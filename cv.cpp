extern "C" {
#define FUSE_USE_VERSION 26
#include <fuse.h>
}

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include "FireSight.hpp"
#include "FireLog.h"
#include "firefuse.h"
#include "version.h"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace cv;
using namespace std;
using namespace firesight;

typedef enum {UI_STILL, UI_VIDEO} UIMode;

size_t MAX_SAVED_IMAGE = 3000000; // empirically chosen to handle 400x400 png images

#define CAMERA_MSTIMEOUT 1000
#define PROCESS_MSTIMEOUT 15000
#define SAVE_MSTIMEOUT 1000

/**
 * Return canonical CVE path. E.g.:
 *   /dev/firefuse/sync/cv/1/gray/calc-offset/save.fire => /cv/1/gray/calc-offset
 *
 * Return empty string if path is not a canonical CVE path
 */
string CVE::cve_path(const char *pPath) {
    if (pPath == NULL) {
        return string();
    }
    const char *pSlash = pPath;
    const char *pCv = NULL;
    const char *pCve = NULL;
    for (const char *s=pPath; *s; s++) {
        if (*s == '/') {
            pSlash = s;
            if (strncmp("/cv/", s, 4) == 0) {
                pCv = s;
                s += 3;
            } else if (strncmp("/cve/", s, 5) == 0) {
                pCve = s;
                s += 4;
            } else if (pCve) {
                break;
            }
        }
    }
    if (!pCve || !pCv) {
        return string();
    }
    if (pSlash <= pCve) {
        return string(pCv);
    }
    return string(pCv, pSlash-pCv);
}

bool is_cv_path(const char *path) {
    for (const char *s = path; s && *s; s++) {
        if (strncmp("/cv", s, 3) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static string camera_profile(const char * path) {
    string pathstr(path);
    string result;
    size_t cvePos = pathstr.find("/cve/");

    if (cvePos != string::npos) {
        size_t slashPos = pathstr.rfind("/", cvePos-1);
        if (slashPos != string::npos) {
            result = pathstr.substr(slashPos+1, cvePos-slashPos-1);
        }
    }
    if (result.size() == 0) {
        LOGERROR1("camera_profile(%s) -> UNKNOWN", path);
        return "UNKNOWN";
    }

    LOGTRACE2("camera_profile(%s) -> %s", path, result.c_str());

    return result;
}

int cve_rename(const char *path1, const char *path2) {
    int res = 0;

    if ( firefuse_isFile(path2, FIREREST_CAMERA_JPG) ) {
        // for raspistill we simply ignore the rename of camera.jpg~ to camera.jpg
        LOGDEBUG2("cve_rename(%s, %s)", path1, path2);
		worker.cameras[0].endCapture();
    } else {
        LOGERROR2("cve_rename(%s, %s) -> -EPERM", path1, path2);
        res = -EPERM;
    }

    return res;
}

int cve_getattr(const char *path, struct stat *stbuf) {
    int res = 0;
	bool trace = FALSE;

    if (firefuse_isFile(path, FIREREST_CAMERA_JPG) || firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {
        res = firefuse_getattr_file(path, stbuf, worker.cameras[0].src_camera_jpg.peek().size(), 0666);
    } else if (firefuse_isFile(path, FIREREST_PROPERTIES_JSON)) {
        res = firefuse_getattr_file(path, stbuf, worker.cve(path).src_properties_json.peek().size(), 0666);
    } else if (firefuse_isFile(path, FIREREST_OUTPUT_JPG)) {
        res = firefuse_getattr_file(path, stbuf, worker.cameras[0].src_output_jpg.peek().size(), 0444);
    } else if (firefuse_isFile(path, FIREREST_MONITOR_JPG)) {
        res = firefuse_getattr_file(path, stbuf, worker.cameras[0].src_monitor_jpg.peek().size(), 0444);
    } else if (firefuse_isFile(path, FIREREST_SAVED_PNG)) {
        res = firefuse_getattr_file(path, stbuf, worker.cve(path).src_saved_png.peek().size(), 0666);
    } else if (firefuse_isFile(path, FIREREST_SAVE_FIRE)) {
        size_t bytes = max(MIN_SAVE_SIZE, worker.cve(path).src_save_fire.peek().size());
        res = firefuse_getattr_file(path, stbuf, bytes, 0444);
    } else if (firefuse_isFile(path, FIREREST_PROCESS_FIRE)) {
        size_t bytes = max(MIN_PROCESS_SIZE, worker.cve(path).src_process_fire.peek().size());
        res = firefuse_getattr_file(path, stbuf, bytes, 0444);
    } else if (firefuse_isFile(path, FIREREST_FIRESIGHT_JSON)) {
        res = firefuse_getattr_file(path, stbuf, worker.cve(path).src_firesight_json.peek().size(), 0444);
    } else {
		trace = TRUE;
        res = firerest_getattr_default(path, stbuf);
    }

	if (trace) {
		LOGTRACE3("cve_getattr(%s) stat->st_size:%ldB -> %d", path, (ulong) stbuf->st_size, res);
	} else {
		LOGDEBUG3("cve_getattr(%s) stat->st_size:%ldB -> %d", path, (ulong) stbuf->st_size, res);
	}
    return res;
}

static SmartPointer<char> buildErrorMessage(const char* fmt, const char *path, const char * ex) {
    LOGERROR2(fmt, path, ex);
    string errMsg = "{\"error\":\"";
    errMsg.append(ex);
    errMsg.append("\"}");
    return SmartPointer<char>((char *)errMsg.c_str(), errMsg.size()+1);
}

int CVE::process(BackgroundWorker *pWorker) {
    int result = 0;

    double sStart = BackgroundWorker::seconds();
    LOGTRACE1("cve_process(%s) init", name.c_str());
    string pathBuf(name);
    const char *path = pathBuf.c_str();
    SmartPointer<char> pipelineJson(src_firesight_json.get());
    char *pModelStr = NULL;
    SmartPointer<char> jsonResult;
    vector<void*> gc;
    try {
        Pipeline pipeline(pipelineJson.data(), Pipeline::JSON);
        Mat image = _isColor ?
                    pWorker->cameras[0].src_camera_mat_bgr.get() :
                    pWorker->cameras[0].src_camera_mat_gray.get();
        ArgMap argMap;
        json_t *pProperties = NULL;
        struct stat propertiesStat;
        string propertiesString;
        SmartPointer<char> properties_json(src_properties_json.get());
        if (properties_json.size()) {
            propertiesString = string(properties_json.data(), properties_json.data()+properties_json.size());
            json_error_t jerr;
            pProperties = json_loads(propertiesString.c_str(), 0, &jerr);
            if (json_is_object(pProperties)) {
                const char * key;
                json_t *pValue;
                json_object_foreach(pProperties, key, pValue) {
                    const char *valueStr = "(unknown)";
                    if (json_is_string(pValue)) {
                        valueStr = json_string_value(pValue);
                    } else {
                        valueStr = json_dumps(pValue, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0)|JSON_ENCODE_ANY);
                        gc.push_back((void *) valueStr); // garbage collection list
                    }
                    LOGTRACE3("CVE::process(%s) argMap[%s]=\"%s\"", path, key, valueStr);
                    argMap[key] = valueStr;
                }
            } else {
                LOGERROR2("cve_process(%s) Could not load properties: %s", name.c_str(), propertiesString.c_str());
                throw "could not load properties";
            }
        }
        string savedPath(fuse_root);
        savedPath += name;
        savedPath += FIREREST_SAVED_PNG;
        LOGTRACE3("CVE::process(%s) argMap[%s]=\"%s\"", path, "saved", savedPath.c_str());
        argMap["saved"] = savedPath.c_str();

        LOGTRACE1("cve_process(%s) process begin", path);
        json_t *pModel = pipeline.process(image, argMap);
        LOGTRACE1("cve_process(%s) process end", path);
        if (pProperties) {
            json_decref(pProperties);
        }
        int jsonIndent = 0;
        pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
        size_t modelLen = pModelStr ? strlen(pModelStr) : 0;
        size_t bytes = max(MIN_PROCESS_SIZE, modelLen);
        jsonResult = SmartPointer<char>(pModelStr, modelLen, SmartPointer<char>::ALLOCATE, bytes, ' ');
        free(pModelStr);
        json_decref(pModel);
        pWorker->cameras[0].setOutput(image);
        double sElapsed = BackgroundWorker::seconds() - sStart;
        LOGDEBUG3("cve_process(%s) -> JSON %ldB %0.3fs", path, modelLen, sElapsed);
    } catch (const char * ex) {
        jsonResult = buildErrorMessage("cve_process(%s) EXCEPTION: %s", path, ex);
    } catch (string ex) {
        jsonResult = buildErrorMessage("cve_process(%s) EXCEPTION: %s", path, ex.c_str());
    } catch (json_error_t ex) {
        string errMsg(ex.text);
        char buf[200];
        snprintf(buf, sizeof(buf), "%s line:%d", ex.text, ex.line);
        errMsg.append(buf);
        jsonResult = buildErrorMessage("cve_process(%s) JSON EXCEPTION: %s", path, errMsg.c_str());
    } catch (...) {
        jsonResult = buildErrorMessage("cve_process(%s) UNKNOWN EXCEPTION: %s", path, "UNKOWN EXCEPTION");
    }
    src_process_fire.post(jsonResult);
    for (int i = 0; i < gc.size(); i++) {
        free(gc[i]);
    }
    return result;
}

int cve_open(const char *path, struct fuse_file_info *fi) {
    int result = 0;
    CameraNode &camera = worker.cameras[0];

    if (firefuse_isFile(path, FIREREST_PROPERTIES_JSON)) {
        if (verifyOpenRW(path, fi, &result)) {
            fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.cve(path).src_properties_json.get());
        }
    } else if (firefuse_isFile(path, FIREREST_CAMERA_JPG) || 
			firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {
        if (verifyOpenRW(path, fi, &result)) {
            if ((fi->flags & 3 ) == O_WRONLY) {
                SmartPointer<char> empty_buffer(NULL, MAX_SAVED_IMAGE);
                empty_buffer.setSize(0);
                fi->fh = (uint64_t) (size_t) new SmartPointer<char>(empty_buffer);
                LOGDEBUG2("cve_open(%s, O_WRONLY) new:@%lx", path, (size_t) empty_buffer.data());
            } else { // O_RDONLY
                if (FireREST::isSync(path)) {
					LOGDEBUG1("cve_open(%s,O_RDONLY) sync capture() for camera", path);
					worker.cameras[0].capture();
                    fi->fh = (uint64_t) (size_t) 
						new SmartPointer<char>(camera.src_camera_jpg.get_sync(CAMERA_MSTIMEOUT));
					LOGDEBUG2("cve_open(%s,O_RDONLY) sync capture() peek:%ldB", 
						path, (long) camera.src_camera_jpg.peek().size());
                } else {
                    fi->fh = (uint64_t) (size_t) new SmartPointer<char>(camera.src_camera_jpg.get());
					LOGDEBUG2("cve_open(%s,O_RDONLY) %ldB", path, (long) camera.src_camera_jpg.peek().size());
                }
            }
        }
    } else if (firefuse_isFile(path, FIREREST_SAVED_PNG)) {
        if (verifyOpenRW(path, fi, &result)) {
            if ((fi->flags & 3 ) == O_WRONLY) {
                SmartPointer<char> saved_png(worker.cve(path).src_saved_png.peek());
                if (saved_png.allocated_size() < MAX_SAVED_IMAGE) {
                    SmartPointer<char> empty_buffer(NULL, MAX_SAVED_IMAGE);
                    empty_buffer.setSize(0);
                    worker.cve(path).src_saved_png.post(empty_buffer);
                    saved_png = empty_buffer;
                    LOGTRACE3("cve_open(%s, O_WRONLY) allocated %ldB @ %lx",
                              path, saved_png.allocated_size(), (size_t) saved_png.data());
                } else {
                    LOGTRACE3("cve_open(%s, O_WRONLY) reusing %ldB @ %lx",
                              path, saved_png.allocated_size(), (size_t) saved_png.data());
                }
                fi->fh = (uint64_t) (size_t) new SmartPointer<char>(saved_png);
            } else {
                fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.cve(path).src_saved_png.get());
            }
        }
    } else if (verifyOpenR_(path, fi, &result)) {
        if (firefuse_isFile(path, FIREREST_PROCESS_FIRE)) {
            if (FireREST::isSync(path)) {
                int count = firerest.incrementProcessCount();
                if (count == 1) {
					LOGDEBUG1("cve_open(%s) capture() for process", path);
					worker.cameras[0].capture();
                    camera.src_camera_jpg.get_sync(CAMERA_MSTIMEOUT);
                    fi->fh = (uint64_t) (size_t) 
						new SmartPointer<char>(worker.cve(path).src_process_fire.get_sync(PROCESS_MSTIMEOUT));
                } else {
                    LOGERROR2("cve_open(%s) EAGAIN operation would block (processCount=%d)", path, count);
                    result = -EAGAIN;
                }
                firerest.decrementProcessCount();
            } else {
                fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.cve(path).src_process_fire.get());
            }
        } else if (firefuse_isFile(path, FIREREST_SAVE_FIRE)) {
            if (FireREST::isSync(path)) {
				LOGDEBUG1("cve_open(%s) capture() for save", path);
				worker.cameras[0].capture();
                camera.src_camera_jpg.get_sync(CAMERA_MSTIMEOUT);
            }
            fi->fh = (uint64_t) (size_t) 
				new SmartPointer<char>(worker.cve(path).src_save_fire.get_sync(SAVE_MSTIMEOUT));
        } else if (firefuse_isFile(path, FIREREST_OUTPUT_JPG)) {
            fi->fh = (uint64_t) (size_t) new SmartPointer<char>(camera.src_output_jpg.get());
        } else if (firefuse_isFile(path, FIREREST_MONITOR_JPG)) {
            fi->fh = (uint64_t) (size_t) new SmartPointer<char>(camera.src_monitor_jpg.get());
        } else if (firefuse_isFile(path, FIREREST_FIRESIGHT_JSON)) {
            fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.cve(path).src_firesight_json.get());
        } else {
            result = -ENOENT;
        }
    }

    switch (-result) {
    case ENOENT:
        LOGERROR1("cve_open(%s) error ENOENT", path);
        break;
    case EACCES:
        LOGERROR1("cve_open(%s) error EACCES", path);
        break;
    default:
        if (fi->fh) {
            fi->direct_io = 1;
            LOGTRACE1("cve_open(%s) direct_io:1", path);
        } else {
            LOGTRACE1("cve_open(%s) direct_io:0", path);
        }
        break;
    }

    return result;
}


int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    size_t sizeOut = size;
    size_t len;
    (void) fi;

    if ( firefuse_isFile(path, FIREREST_CAMERA_JPG) ){
        SmartPointer<char> *pData = (SmartPointer<char> *) fi->fh;
        sizeOut = firefuse_readBuffer(buf, (char *)pData->data(), size, offset, pData->size());
    } else if ( firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {
        SmartPointer<char> *pData = (SmartPointer<char> *) fi->fh;
        sizeOut = firefuse_readBuffer(buf, (char *)pData->data(), size, offset, pData->size());
    } else if ( firefuse_isFile(path, FIREREST_MONITOR_JPG) ||
                firefuse_isFile(path, FIREREST_OUTPUT_JPG) ||
                firefuse_isFile(path, FIREREST_SAVED_PNG) ||
                firefuse_isFile(path, FIREREST_PROCESS_FIRE) ||
                firefuse_isFile(path, FIREREST_SAVE_FIRE) ||
                firefuse_isFile(path, FIREREST_FIRESIGHT_JSON) ||
                firefuse_isFile(path, FIREREST_PROPERTIES_JSON) ||
                FALSE) {
        SmartPointer<char> *pData = (SmartPointer<char> *) fi->fh;
        sizeOut = firefuse_readBuffer(buf, (char *)pData->data(), size, offset, pData->size());
    } else if (fi->fh) { // data file
        FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
        sizeOut = firefuse_readBuffer(buf, pBuffer->pData, size, offset, pBuffer->length);
    } else {
        LOGERROR2("cve_read(%s, %ldB) ENOENT", path, size);
        return -ENOENT;
    }

    LOGTRACE4("cve_read(%s,%ldB,%ld) -> %ldB", path, (long) size, (long) offset, sizeOut);
    return sizeOut;
}

int cve_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi) {
    ASSERTNONZERO(buf);
    ASSERT(bufsize >= 0);
    if (firefuse_isFile(path, FIREREST_PROPERTIES_JSON)) { //properties.json??
        ASSERT(offset == 0);
        SmartPointer<char> data((char *) buf, bufsize);
        worker.cve(path).src_properties_json.post(data);
    } else if (firefuse_isFile(path, FIREREST_CAMERA_JPG) 
		|| firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {//camera.jpg temporary file
        SmartPointer<char> * pImage =  (SmartPointer<char> *) fi->fh;
        if (bufsize + offset > pImage->allocated_size()) {
            LOGWARN4("cve_write(%s,%ldB,%ld) data truncated to fit %ldB", 
				path, bufsize, offset, pImage->allocated_size());
        } else {
            memcpy(pImage->data()+offset, buf, bufsize);
            pImage->setSize(offset+bufsize);
            LOGDEBUG4("cve_write(%s,%ldB,%ld) %ldB total", path, bufsize, offset, pImage->size());
        }
    } else if (firefuse_isFile(path, FIREREST_SAVED_PNG))  { //saved.png?  
        SmartPointer<char> * pImage =  (SmartPointer<char> *) fi->fh;
        if (bufsize + offset > pImage->allocated_size()) {
            LOGWARN4("cve_write(%s,%ldB,%ld) data truncated to fit %ldB", 
				path, bufsize, offset, pImage->allocated_size());
        } else {
            memcpy(pImage->data()+offset, buf, bufsize);
            pImage->setSize(offset+bufsize);
            LOGTRACE4("cve_write(%s,%ldB,%ld) %ldB total", path, bufsize, offset, pImage->size());
        }
    } else {
        LOGERROR3("cve_write(%s,%ldB,%ld) ENOENT", path, bufsize, offset);
        return -ENOENT;
    }

    return bufsize;
}

int cve_release(const char *path, struct fuse_file_info *fi) {
    if (!fi->fh) {
        LOGDEBUG1("cve_release(%s) NULL", path);
        return 0;
    }
    SmartPointer<char> *pSP = (SmartPointer<char> *) fi->fh;
    if (firefuse_isFile(path, FIREREST_CAMERA_JPG)) {
        if ((fi->flags & 3 ) == O_WRONLY) {
            LOGDEBUG3("cve_release(%s,%lx) write:%ldB->camera_jpg", 
				path, (size_t)pSP->data(), pSP->size());
            //worker.cameras[0].accept_new_image(*pSP);
        } else {
            LOGDEBUG3("cve_release(%s,%lx) read:%ldB", path, (size_t)pSP->data(), pSP->size());
        }
        delete pSP;
    } else if (firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {
        if ((fi->flags & 3 ) == O_WRONLY) {
            LOGDEBUG3("cve_release(%s,%lx) write:%ldB->camera_jpg temp", 
				path, (size_t)pSP->data(), pSP->size());
            worker.cameras[0].accept_new_image(*pSP);
        } else {
            LOGDEBUG3("cve_release(%s,%lx) read:%ldB", path, (size_t)pSP->data(), pSP->size());
        }
        delete pSP;
    } else if (
        firefuse_isFile(path, FIREREST_PROCESS_FIRE) ||
        firefuse_isFile(path, FIREREST_MONITOR_JPG) ||
        firefuse_isFile(path, FIREREST_SAVED_PNG) ||
        firefuse_isFile(path, FIREREST_OUTPUT_JPG) ||
        firefuse_isFile(path, FIREREST_FIRESIGHT_JSON) ||
        firefuse_isFile(path, FIREREST_PROPERTIES_JSON) ||
        firefuse_isFile(path, FIREREST_SAVE_FIRE)) {
        LOGDEBUG3("cve_release(%s,%lx) %ldB", path, (size_t)pSP->data(), pSP->size());
        delete pSP;
    } else {
        LOGWARN1("cve_release(%s) UNEXPECTED PATH", path);
    }
    return 0;
}

int cve_truncate(const char *path, off_t size) {
	if (size != 0) {
		LOGERROR2("cve_truncate(%s,%ldB) ignoring size", path, size);
	}
	CameraNode &camera = worker.cameras[0];
    if (firefuse_isFile(path, FIREREST_SAVED_PNG)) {
        LOGDEBUG1("cve_truncate(%s)", path);
        worker.cve(path).src_saved_png.peek().setSize(size);
    } else if (firefuse_isFile(path, FIREREST_CAMERA_JPG)) {
		if (camera.isCapturing()) {
			LOGWARN1("cve_truncate(%s) ignored (capture in progress)", path);
		} else {
			LOGDEBUG1("cve_truncate(%s) => truncated and blocking for capture()", path);
			camera.src_camera_jpg.peek().setSize(0);
			camera.capture();
			camera.src_camera_jpg.get_sync(CAMERA_MSTIMEOUT);
		}
    } else if (firefuse_isFile(path, FIREREST_CAMERA_JPG_TILDE)) {
        LOGDEBUG1("cve_truncate(%s) camera temp file", path);
		camera.src_camera_jpg.peek().setSize(0);
    } else {
        LOGDEBUG2("cve_truncate(%s,%ldB) ignored", path, size);
	}
    return 0;
}

CVE::CVE(string name) {
    this->name = name;
    const char *firesight = "[{\"op\":\"putText\", \"text\":\"CVE::CVE()\"}]";
    src_firesight_json.post(SmartPointer<char>((char *)firesight, strlen(firesight)));
    const char *emptyJson = "{}";
    src_save_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
    src_process_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
    this->_isColor = strcmp("bgr", camera_profile(name.c_str()).c_str()) == 0;
}

CVE::~CVE() {
}


int CVE::save(BackgroundWorker *pWorker) {
    double sStart = BackgroundWorker::seconds();
    string errMsg;

    Mat image = _isColor ?
                pWorker->cameras[0].src_camera_mat_bgr.get() :
                pWorker->cameras[0].src_camera_mat_gray.get();
    size_t bytes = 0;
    if (image.rows && image.cols) {
        vector<uchar> pngBuf;
        vector<int> param = vector<int>(2);
        param[0] = CV_IMWRITE_PNG_COMPRESSION;
        param[1] = 3;//default(3)  0-9.
        imencode(".png", image, pngBuf, param);
        bytes = pngBuf.size();
        SmartPointer<char> png((char *)pngBuf.data(), bytes);
        src_saved_png.post(png);
        putText(image, "Saved", Point(7, image.rows-6), FONT_HERSHEY_SIMPLEX, 2, Scalar(0,0,0), 3);
        putText(image, "Saved", Point(5, image.rows-8), FONT_HERSHEY_SIMPLEX, 2, Scalar(255,255,255), 3);
        pWorker->cameras[0].setOutput(image);
        LOGTRACE4("CVE::save(%s) %s image saved (%ldB) %0.3fs", name.c_str(), _isColor ? "color" : "gray", bytes, BackgroundWorker::seconds() - sStart);
    } else {
        errMsg = "CVE::save(";
        errMsg.append(name);
        errMsg.append(") => cannot save empty camera image");
    }

    char jsonBuf[255];
    if (errMsg.empty()) {
        snprintf(jsonBuf, sizeof(jsonBuf), "{\"bytes\":%ld}", bytes);
    } else {
        snprintf(jsonBuf, sizeof(jsonBuf), "{\"bytes\":%ld,\"message\":\"%s\"}",
                 bytes, errMsg.c_str());
    }
    size_t jsonBytes = max(MIN_SAVE_SIZE, (size_t) strlen(jsonBuf));
    SmartPointer<char> json(jsonBuf, strlen(jsonBuf), SmartPointer<char>::ALLOCATE, jsonBytes, ' ');
    src_save_fire.post(json);
    double sElapsed = BackgroundWorker::seconds() - sStart;
    LOGDEBUG3("CVE::save(%s) -> %ldB %0.3fs", name.c_str(), (ulong) json.size(), sElapsed);

    return errMsg.empty() ? 0 : -ENOENT;
}

