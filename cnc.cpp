#include "FireSight.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "firefuse.h"
#include "version.h"

int SERIAL_TIMEOUT_SECS = 10;

bool is_cnc_path(const char *path) {
    for (const char *s = path; s && *s; s++) {
        if (strncmp("/cnc", s, 4) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

int cnc_getattr(const char *path, struct stat *stbuf) {
    int res = 0;
    if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
        res = firefuse_getattr_file(path, stbuf, worker.dce(path).src_gcode_fire.peek().size(), 0666);
    } else {
        res = firerest_getattr_default(path, stbuf);
    }
    if (res == 0) {
        LOGTRACE2("cnc_getattr(%s) stat->st_size:%ldB -> OK", path, (ulong) stbuf->st_size);
    }
    return res;
}

int cnc_open(const char *path, struct fuse_file_info *fi) {
    int result = 0;

    if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
        if (verifyOpenRW(path, fi, &result)) {
            if ((fi->flags&3) == O_WRONLY && worker.dce(path).snk_gcode_fire.isFresh()) {
                LOGTRACE("snk_gcode_fire.isFresh()");
                result = -EAGAIN;
            } else {
                fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.dce(path).src_gcode_fire.get());
            }
        }
    }
    return result;
}

int cnc_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    int result = 0;
    return result;
}

int cnc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    size_t sizeOut = size;
    size_t len;
    (void) fi;

    if (firefuse_isFile(path, FIREREST_GCODE_FIRE) ||
            firefuse_isFile(path, FIREREST_PROPERTIES_JSON) ||
            FALSE) {
        SmartPointer<char> *pData = (SmartPointer<char> *) fi->fh;
        sizeOut = firefuse_readBuffer(buf, (char *)pData->data(), size, offset, pData->size());
    } else {
        LOGERROR2("cnc_read(%s, %ldB) ENOENT", path, size);
        return -ENOENT;
    }

    LOGTRACE3("cnc_read(%s, %ldB) -> %ldB", path, size, sizeOut);
    return sizeOut;
}

// FireFUSE handler for cnc path 
int cnc_write(const char *path, const char *buf, size_t bytes, off_t offset, struct fuse_file_info *fi) {
    assert(buf != NULL);
    assert(bytes >= 0);
    SmartPointer<char> data((char *) buf, bytes);
    if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
		DCE &dce = worker.dce(path);
		dce.setSync(FireREST::isSync(path));
		dce.send_request(data);
        string cmd(buf, bytes);
		// ignore offset because we're writing to a raw output-only device that is not random-access
        LOGTRACE3("DCE::cnc_write(%s) offset:%ld sync:%d", cmd.c_str(), (long) offset, dce.isSync());
        json_t * response = json_object();
        json_object_set(response, "status", json_string("ACTIVE"));
        json_object_set(response, "gcode", json_string(cmd.c_str()));
        char *responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
        worker.dce(path).src_gcode_fire.post(
			SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
        json_decref(response);
    } else {
        LOGERROR2("cnc_write(%s,%ldB) ENOENT", path, bytes);
        return -ENOENT;
    }

    return bytes;
}

int cnc_release(const char *path, struct fuse_file_info *fi) {
    int result = 0;
    LOGTRACE1("cnc_release(%s)", path);
    if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
        if (fi->fh) {
            free( (SmartPointer<char> *) fi->fh);
        }
    }
    return 0;
}


int cnc_truncate(const char *path, off_t size) {
    int result = 0;
    return result;
}

//////////////////////////////// DCE //////////////////////////////////

#define CMDMAX 255
#define INBUFMAX 3000
#define JSONMAX 3000

DCE::DCE(string name) {
    this->name = name;
	this->is_sync = FALSE;
    this->serial_fd = -1;
    this->jsonBuf = (char*)malloc(JSONMAX+3); // +nl, cr, EOS
    this->inbuf = (char*)malloc(INBUFMAX+1); // +EOS
	LOGTRACE2("DCE::DCE(%s) isSync:%d", name.c_str(), is_sync);
    init();
}

DCE::~DCE() {
    if (jsonBuf) {
        free(jsonBuf);
    }
    if (inbuf) {
        free(inbuf);
    }
}

void DCE::setSync(bool value) {
	if (value != is_sync) {
		if (activeRequests > 0) {
			LOGWARN2("DCE::setSync(%d) clearing activeRequests:%d", value, activeRequests);
			activeRequests = 0;
		} 
		is_sync = value;
		LOGINFO1("DCE::setSync(%d)", value);
	}
}

void DCE::init() {
    LOGINFO1("DCE::init(%s)", name.c_str());
    const char *emptyJson = "{}";
    src_gcode_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
    if (serial_fd >= 0) {
        LOGINFO2("DCE::init(%s) close serial port: %s", name.c_str(), serial_path.c_str());
        close(serial_fd);
        serial_fd = -1;
    }
    jsonLen = 0;
    jsonDepth = 0;
    inbuflen = 0;
    inbufEmptyLine = 0;
    activeRequests = 0;
}

void DCE::send_request(SmartPointer<char> &data) {
	long msStart = millis();
	snk_gcode_fire.post(data);
	if (is_sync) {
		double seconds = 0;
		while (snk_gcode_fire.isFresh() || activeRequests > 0) {
			seconds = (millis() - msStart)/1000.0;
			if (seconds > SERIAL_TIMEOUT_SECS) {
				break;
			}
			usleep(100*1000);
		}
		if (activeRequests > 0 && seconds > SERIAL_TIMEOUT_SECS) {
			LOGERROR1("DCE::send_request() SERIAL TIMEOUT:%ds", SERIAL_TIMEOUT_SECS);
		} else {
			LOGDEBUG2("DCE::send_request() complete:%gs activeRequests:%d", seconds, activeRequests);
		}
	}
}

/**
 * Return canonical DCE path. E.g.:
 *   /dev/firefuse/sync/cnc/marlin/gcode.fire => /cnc/marlin
 *   /dev/firefuse/cnc/marlin/gcode.fire => /cnc/marlin
 *
 * There can be at most one DCE for each serial port,
 * since the serial_reader_thread must have exclusive access
 * to that port.
 *
 * Return empty string if path is not a canonical DCE path
 */
string DCE::dce_path(const char *pPath) {
    if (pPath == NULL) {
        return string();
    }
    const char *pSlash = NULL;
    const char *pDce = NULL;
    for (const char *s=pPath; *s; s++) {
        if (*s == '/') {
            pSlash = s;
            if (strncmp("/cnc/", s, 5) == 0) {
                pDce = s;
                s += 4;
            } else if (pDce) {
                break;
            }
        }
    }
    if (!pDce) {						// not a cnc path => ""
        return string(); 
    }
    if (pSlash <= pDce) {				// e.g., /sync/cnc/marlin => "/cnc/marlin"
        return string(pDce);
    }
    return string(pDce, pSlash-pDce);	// e.g., /cnc/marlin/gcode.fire => "/cnc/marlin"
}

json_t *json_string(char *value, size_t length) {
    string str(value, length);
    return json_string(str.c_str());
}

int DCE::serial_init() {
    if (serial_fd >= 0) {
        LOGINFO1("DCE::serial_init(%s) already open", name.c_str());
        return 0; // already started
    }
    if (serial_path.empty()) {
        LOGINFO1("DCE::serial_init(%s) no serial configuration", name.c_str());
        return 0;
    }
    if (0==serial_path.compare("mock")) {
        LOGINFO1("DCE::serial_init(%s) mock serial", name.c_str());
        return 0;
    }

    const char * path = serial_path.c_str();
    struct stat statbuf;
    int rc = 0;

    if (stat(path, &statbuf) == 0) {
        LOGINFO1("DCE::serial_init(%s)", path);
        char cmdbuf[256];

        if (serial_stty.empty()) {
            LOGINFO1("DCE::serial_init(%s) serial configuration unchanged", path);
        } else {
            snprintf(cmdbuf, sizeof(cmdbuf), 
			    "stty 0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0 -F %s", path);
            LOGDEBUG2("DCE::serial_init(%s) %s (first call may fail)", path, cmdbuf);
            rc = BackgroundWorker::callSystem(cmdbuf);
            if (rc) {
                LOGINFO3("DCE::serial_init(%s) %s -> %d RETRYING...", path, cmdbuf, rc);
                rc = BackgroundWorker::callSystem(cmdbuf);
            }
            if (rc) {
                LOGERROR2("DCE::serial_init(%s) clear serial port failed -> %d", path, rc);
                return rc;
            }

            snprintf(cmdbuf, sizeof(cmdbuf), "stty %s -F %s", serial_stty.c_str(), path);
            LOGINFO2("DCE::serial_init(%s) %s", path, cmdbuf);
            rc = BackgroundWorker::callSystem(cmdbuf);
            if (rc) {
                LOGERROR3("DCE::serial_init(%s) %s -> %d", path, cmdbuf, rc);
                return rc;
            }
        }

        LOGDEBUG1("DCE::serial_init:open(%s)", path);
        serial_fd = open(path, O_RDWR | O_ASYNC | O_NONBLOCK);
        if (serial_fd < 0) {
            rc = errno;
            LOGERROR2("DCE::serial_init:open(%s) failed -> %d", path, rc);
            return rc;
        }
        LOGINFO1("DCE::serial_init(%s) opened for write", path);

        LOGRC(rc, "pthread_create(serial_reader_thread) -> ", pthread_create(&tidReader, NULL, &serial_reader_thread, this));
        LOGINFO("DCE::serial_init() yielding to serial_reader_thread");
		usleep(500*1000);

        LOGINFO("DCE::serial_init() sending device_config");
		int curLogLevel = logLevel;
		firelog_level(FIRELOG_TRACE);
        for (int i=0; i < serial_device_config.size(); i++) {
            string config = serial_device_config[i];
            serial_send(config.c_str(), config.size());
        }
		firelog_level(curLogLevel);
    } else {
        LOGERROR1("DCE::serial_init(%s) No device", path);
    }

    return rc;
}

void DCE::send_line(string request, json_t*response) {
    if (serial_path.empty()) {
        LOGWARN1("DCE::send_line(%s) serial_path has not been configured", request.c_str());
        json_object_set(response, "status", json_string("WARNING"));
        json_object_set(response, "response", json_string("Serial path not configured"));
    } else if (0==serial_path.compare("mock")) {
        LOGTRACE2("DCE::send_line(%s) serial_path:%s", request.c_str(), serial_path.c_str());
        json_object_set(response, "status", json_string("DONE"));
        json_object_set(response, "response", json_string("Mock response"));
    } else {
        serial_send(request.c_str(), request.size());
    }
}

vector<string> DCE::gcode_lines(const string &gcode) {
    vector<string> lines;
    stringstream ss(gcode);
    string line;
    while (getline(ss, line)) {
        if (line.empty()) {
            continue; // skip blank lines
        }
        line.erase(line.begin(), find_if(line.begin(), line.end(), not1(ptr_fun<int, int>(isspace)))); // ltrim
        lines.push_back(line);
    }
    return lines;
}

int DCE::gcode(BackgroundWorker *pWorker) {
    if (snk_gcode_fire.isFresh()) {
        SmartPointer<char> request = snk_gcode_fire.get();
        string gcode(request.data(), request.size());
        vector<string> lines(gcode_lines(gcode));

        for (int i = 0; i < lines.size(); i++) {
            json_t *response = json_object();
            json_object_set(response, "status", json_string("ACTIVE"));
            json_t *json_cmd = json_string(lines[i].c_str());
            json_object_set(response, "gcode", json_cmd);

            send_line(lines[i], response);

            char * responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
            LOGTRACE2("DCE::gcode(%s) -> %s", json_string_value(json_cmd), responseStr);
            src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
            json_decref(response);
        }
    }

    return 0;
}

int DCE::post_serial_status(const char *line) {
	bool isAck = serial_ack.compare(0,serial_ack.size(),inbuf) == 0;

	if (serial_reader_buf.size() > 0) { // Accumulate multi-line sync response
		serial_reader_buf += "\n";
	}
	serial_reader_buf += line;

	const char * status = "ACTIVE";
    if (isAck) { // requested action is complete
		status = "ACK";
		activeRequests = max(0,--activeRequests);
    }

	if (!is_sync || isAck) {
		const char *s = serial_reader_buf.c_str();
		json_t *response = json_object();
		json_object_set(response, "status", json_string(status));
		json_object_set(response, "response", json_string(s));
		char * responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
		src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
		json_decref(response);
		LOGTRACE2("DCE::post_serial_status(%s) activeRequests:%d", responseStr, activeRequests);
		serial_reader_buf = "";	
	} else {
		LOGTRACE2("DCE::post_serial_status(%s) activeRequests:%d", line, activeRequests);
	}

    return 0;
}

const char * DCE::read_json() {
    int wait = 0;
    while (jsonDepth > 0) {
        LOGDEBUG1("DCE::read_json() waiting for JSON %d", wait++);
        sched_yield(); // wait for completion
        if (wait > 10) {
            LOGERROR("DCE::read_json() unterminated JSON");
            return "{\"error\":\"unterminated JSON\"}";
        }
    }
    jsonBuf[jsonLen] = 0;
    if (jsonLen > 0) {
        jsonBuf[jsonLen++] = '\n';
        jsonBuf[jsonLen++] = 0;
    }
    return jsonBuf;
}

int DCE::serial_send(const char *buf, size_t bufsize) {
#define LOGBUFMAX 100
    char logmsg[LOGBUFMAX+4];
    for (; bufsize > 0; bufsize--) { // strip leading whitespace
        char c = buf[bufsize-1];
        if (c!='\n' && c!='\r' && c!='\t' && c!=' ') {
            break;
        }
    }

    for (int i=0; i < bufsize; i++) {
        uchar c = (uchar) buf[i];
        if (i < LOGBUFMAX) {
            logmsg[i] = c;
        }
    }

    if (bufsize > LOGBUFMAX) { // add ... when logging long text
        logmsg[LOGBUFMAX] = '.';
        logmsg[LOGBUFMAX+1] = '.';
        logmsg[LOGBUFMAX+2] = '.';
        logmsg[LOGBUFMAX+3] = 0;
    } else {
        logmsg[bufsize] = 0;
    }
    activeRequests++;
    LOGDEBUG4("DCE::serial_send(%s) %ldB sync:%d activeRequests:%d", logmsg, bufsize, is_sync, activeRequests);
    size_t rc = write(serial_fd, buf, bufsize);
    if (rc == bufsize) {
        rc = serial_send_eol(buf, bufsize);
    } else {
        LOGERROR2("DCE::serial_send(%s) -> [%ld]", logmsg, rc);
    }
    return rc < 0 ? rc : 0;
}

int DCE::serial_send_eol(const char *buf, size_t bufsize) {
    char lastChar = buf[bufsize-1];
    if (lastChar != '\r' && lastChar != '\n') {
        LOGTRACE2("DCE::serial_send_eol() sync:%d activeRequests:%d", is_sync, activeRequests);
        size_t rc = write(serial_fd, "\r", 1);
        if (rc != 1) {
            LOGERROR1("DCE::serial_send_eol() -> [%ld]", rc);
            return -EIO;
        }
    }
    return 0;
}

// Add the given character to jsonBuf if it is the inner part of the json response
#define ADD_JSON(c) \
      if (jsonDepth > 1) {\
        jsonBuf[jsonLen++] = c; \
        if (jsonLen >= JSONMAX) { \
          LOGWARN1("Maximum JSON length is %d", JSONMAX); \
          return 0; \
        } \
        jsonBuf[jsonLen] = 0;\
      }

int DCE::serial_read_char(int c) {
    switch (c) {
    case 'a':
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'C':
    case 'd':
    case 'D':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'h':
    case 'H':
    case 'i':
    case 'I':
    case 'j':
    case 'J':
    case 'k':
    case 'K':
    case 'l':
    case 'L':
    case 'm':
    case 'M':
    case 'n':
    case 'N':
    case 'o':
    case 'O':
    case 'p':
    case 'P':
    case 'q':
    case 'Q':
    case 'r':
    case 'R':
    case 's':
    case 'S':
    case 't':
    case 'T':
    case 'u':
    case 'U':
    case 'v':
    case 'V':
    case 'w':
    case 'W':
    case 'x':
    case 'X':
    case 'y':
    case 'Y':
    case 'z':
    case 'Z':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case '-':
    case '_':
    case '/':
    case '{':
    case '}':
    case '(':
    case ')':
    case '[':
    case ']':
    case '<':
    case '>':
    case '"':
    case '\'':
    case ':':
    case ',':
    case ' ':
    case '\t':
        if (c == '{') {
            if (jsonDepth++ <= 0) {
                jsonLen = 0;
            }
            ADD_JSON(c);
        } else if (c == '}') {
            ADD_JSON(c);
            if (--jsonDepth < 0) {
                jsonDepth = 0;
                LOGWARN2("DCE::serial_read_char(%c) invalid JSON %s", (int) c, jsonBuf);
                //return 0;
            }
        } else {
            ADD_JSON(c);
        }
        if (inbuflen >= INBUFMAX) {
            inbuf[INBUFMAX] = 0;
            LOGERROR2("DCE::serial_read_char(%c) overflow %s", (int) c, inbuf);
            break;
        } else {
            inbuf[inbuflen] = c;
            inbuflen++;
            LOGTRACE4("DCE::serial_read_char(%x %c) %d %d", (int) c, (int) c, jsonDepth, jsonLen);
        }
        break;
    case EOF:
        inbuf[inbuflen] = 0;
        inbuflen = 0;
        LOGERROR1("DCE::serial_read_char(%s) [EOF]", inbuf);
        return 0;
    case '\n':
        inbuf[inbuflen] = 0;
        if (inbuflen) { // discard blank lines
			post_serial_status(inbuf);
        } else {
            inbufEmptyLine++;
            if (inbufEmptyLine % 1000 == 0) {
                LOGWARN1("DCE::serial_read_char() skipped %ld blank lines", (long) inbufEmptyLine);
            }
        }
        inbuflen = 0;
        break;
    case '\r':
        // skip
        break;
    default:
        // discard unexpected character (probably wrong baud rate)
        LOGTRACE2("DCE::serial_read_char(%x ?)", (int) c, (int) c);
        break;
    }
    return 1;
}

void * DCE::serial_reader_thread(void *arg) {
#define READBUFLEN 100
    char readbuf[READBUFLEN];
    DCE *pDce = (DCE*) arg;

    LOGINFO("DCE::serial_reader_thread() listening...");

    if (pDce->serial_fd >= 0) {
        char c;
        char loop = TRUE;
        while (loop) {
            int rc = read(pDce->serial_fd, readbuf, READBUFLEN);
            if (rc < 0) {
                if (errno == EAGAIN) {
                    sched_yield();
                    continue;
                }
                LOGERROR2("DCE::serial_reader_thread(%s) [ERRNO:%d]", pDce->inbuf, errno);
                break;
            } else if (rc == 0) {
                sched_yield(); // nothing available to read
                continue;
            } else  {
                for (int i = 0; i < rc; i++) {
                    if (!pDce->serial_read_char(readbuf[i])) {
                        loop = FALSE;
                        break;
                    }
                }
            }
        }
    }

    LOGINFO("DCE::serial_reader_thread(EXIT) /////// SERIAL PORT LISTENER STOPPED /////////");
    return NULL;
}

//////////////////////////// TINYG ////////////////

int tinyg_hash(const char *value, size_t len) {
    while (len > 0) {
        if (value[--len] == ',') {
            break;
        }
    }

    uint32_t h = 0;
    for (int i = 0; i < len; i++) {
        h = h * 31 + value[i];
    }

    return h % 9999;
}
