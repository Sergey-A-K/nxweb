/*
 * Copyright (c) 2011-2012 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXWEB.
 *
 * NXWEB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXWEB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.
 */





#include "nxweb/nxweb.h"
#include "nxweb/misc.h"

#include <python3.6m/Python.h>

#define MODULE_NAME "nxwebpy3"
#define FUNC_NAME "_nxweb_on_request"

static const char* project_path="."; // defaults to workdir
static const char* wsgi_application;
static const char* virtualenv_path="";



static void on_config(const nx_json* js) {
    if (nxweb_main_args.python3_root)
        project_path=nxweb_main_args.python3_root;
    else if (js) {
        const char* v=nx_json_get(js, "project_path")->text_value;
        if (v) project_path=v;
    }
    if (nxweb_main_args.python3_wsgi_app)
        wsgi_application=nxweb_main_args.python3_wsgi_app;
    else if (js) {
        const char* v=nx_json_get(js, "wsgi_application")->text_value;
        if (v) wsgi_application=v;
    }
    if (nxweb_main_args.python3_virtualenv_path)
        virtualenv_path=nxweb_main_args.python3_virtualenv_path;
    else if (js) {
        const char* v=nx_json_get(js, "virtualenv_path")->text_value;
        if (v) virtualenv_path=v;
    }
    nxweb_log_info("Python3: project_path='%s' wsgi_application='%s', virtualenv_path='%s'",
        project_path, wsgi_application, virtualenv_path);
}



static PyThreadState* py3_main_thread_state;
static PyObject* py3_module;
static PyObject* py3_nxweb_on_request_func;
static wchar_t*  py3_prog_name;
static wchar_t **py3_argv;

static int on_startup() {
    if (!wsgi_application || !*wsgi_application) {
        nxweb_log_error("Python3: wsgi_application not defined! Skipping init!");
        return 0;
    }

    struct stat fi;
    static const char* prog_name="python3/nxwebpy3.py";
    if (stat(prog_name, &fi)==-1) {
#ifdef NXWEB_LIBDIR
        prog_name=NXWEB_LIBDIR "/nxwebpy3.py";
        if (stat(prog_name, &fi)==-1) {
#endif
            nxweb_log_error("Python3: %s is missing! Skipping init!", prog_name);
            return 0;
#ifdef NXWEB_LIBDIR
        } else nxweb_log_info("Python3: use default prog_name='%s'", prog_name);
#endif
    } else nxweb_log_info("Python3: use custom prog_name='%s'", prog_name);

    py3_prog_name = Py_DecodeLocale(prog_name, NULL);
    Py_SetProgramName(py3_prog_name);
    Py_Initialize();
    PyEval_InitThreads(); // initialize thread support
    py3_argv = PyMem_Malloc(sizeof(wchar_t*)*4);
    py3_argv[0]= Py_DecodeLocale(prog_name, NULL);
    py3_argv[1]= Py_DecodeLocale(project_path, NULL);
    py3_argv[2]= Py_DecodeLocale(wsgi_application, NULL);
    py3_argv[3]= Py_DecodeLocale(virtualenv_path, NULL);
    PySys_SetArgv(4, py3_argv);
    PyObject* py3_module_name=PyUnicode_FromString(MODULE_NAME);
    assert(py3_module_name);
    py3_main_thread_state=PyThreadState_Get(); // save a pointer to the main PyThreadState object
    py3_module=PyImport_Import(py3_module_name);
    Py_DECREF(py3_module_name);

    if (!py3_module || !PyModule_Check(py3_module)) {
        PyErr_Print();
        exit(0);
    }

    py3_nxweb_on_request_func=PyObject_GetAttrString(py3_module, FUNC_NAME);
    assert(py3_nxweb_on_request_func);
    assert(py3_nxweb_on_request_func && PyCallable_Check(py3_nxweb_on_request_func));
    PyEval_ReleaseLock(); // release the lock
    return 0;
}


static void on_shutdown() {
    if (!py3_module) return; // not initialized
    // shut down the interpreter
    nxweb_log_info("Python3: shutting down");
    // PyEval_AcquireLock();
    PyThreadState_Swap(py3_main_thread_state);
    Py_XDECREF(py3_nxweb_on_request_func);
    Py_XDECREF(py3_module);
    PyMem_Free(py3_argv);
    PyMem_RawFree(py3_prog_name);
    Py_Finalize();
    nxweb_log_info("Python3 finalized");
}






#define NXWEB_MAX_PYTHON_UPLOAD_SIZE 50000000

static const char python3_handler_key; // variable's address only matters
#define PYTHON_HANDLER_KEY ((nxe_data)&python3_handler_key)

static void python3_request_data_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data) {
    nxd_fwbuffer* fwb=data.ptr;
    if (fwb && fwb->fd) {
        // close temp upload file
        close(fwb->fd);
        fwb->fd=0;
    }
}

static nxweb_result python3_on_post_data(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
    if (req->content_length>0 && req->content_length<NXWEB_MAX_REQUEST_BODY_SIZE) {
        // fallback to default in-memory buffering
        return NXWEB_NEXT;
    }
    if (!conn->handler->dir) {
        nxweb_log_warning("Python3: handler temp upload file dir not set => skipping file buffering for %s", req->uri);
        return NXWEB_NEXT;
    }
    nxe_ssize_t upload_size_limit=conn->handler->size? conn->handler->size : NXWEB_MAX_PYTHON_UPLOAD_SIZE;
    if (req->content_length > upload_size_limit) {
        nxweb_send_http_error(resp, 413, "Request Entity Too Large");
        resp->keep_alive=0; // close connection
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
    }
    char* fname_template=nxb_alloc_obj(req->nxb, strlen(conn->handler->dir)+sizeof("/py3_upload_tmp_XXXXXX")+1);
    strcat(strcpy(fname_template, conn->handler->dir), "/py3_upload_tmp_XXXXXX");
    if (nxweb_mkpath(fname_template, 0755)==-1) {
        nxweb_log_error("Python3: can't create path to temp upload file %s; check permissions", fname_template);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        resp->keep_alive=0; // close connection
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
    }
    int fd=mkstemp(fname_template);
    if (fd==-1) {
        nxweb_log_error("Python3: can't open (mkstemp()) temp upload file for %s", req->uri);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        resp->keep_alive=0; // close connection
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
    }
    unlink(fname_template); // auto-delete on close()
    nxd_fwbuffer* fwb=nxb_alloc_obj(req->nxb, sizeof(nxd_fwbuffer));
    nxweb_set_request_data(req, PYTHON_HANDLER_KEY, (nxe_data)(void*)fwb, python3_request_data_finalize);
    nxd_fwbuffer_init(fwb, fd, upload_size_limit);
    conn->hsp.cls->connect_request_body_out(&conn->hsp, &fwb->data_in);
    conn->hsp.cls->start_receiving_request_body(&conn->hsp);
    return NXWEB_OK;
}

static nxweb_result python3_on_post_data_complete(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
    // nothing to do here
    // keep temp file open
    return NXWEB_OK;
}


static inline void dict_set(PyObject* dict, const char* key, PyObject* val) {
    PyDict_SetItemString(dict, key, val);
    Py_XDECREF(val);
}


#define PYOBJECT_CHECK(obj, label) \
    if (!obj) { \
        PyErr_Print(); \
        nxweb_log_error("Python3: PyObject==NULL!"); \
        goto label; \
    }

static nxweb_result python3_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
    nxb_buffer* nxb=req->nxb;
    nxweb_handler* handler=conn->handler;
    const char* request_uri=req->uri;
    char* query_string=strchr(request_uri, '?');
    int ulen=query_string ? (query_string-request_uri) : strlen(request_uri);
    if (query_string) query_string++;
    int pfxlen=req->path_info? (req->path_info - req->uri) : 0;
    int plen=ulen-pfxlen;
    const char* path_info=request_uri+pfxlen;
    if (handler->uri && *handler->uri) {
        pfxlen=strlen(handler->uri);
        ulen=pfxlen+plen;
        char* u=nxb_alloc_obj(nxb, ulen+1);
        memcpy(u, handler->uri, pfxlen);
        memcpy(u+pfxlen, path_info, plen);
        u[ulen]='\0';
        request_uri=u;
        path_info=request_uri+pfxlen;
    }
    const char* host_port=req->host? strchr(req->host, ':') : 0;
    int content_fd=0;
    if (req->content_length) {
        nxd_fwbuffer* fwb=nxweb_get_request_data(req, PYTHON_HANDLER_KEY).ptr;
        if (fwb) {
            if (fwb->error || fwb->size > fwb->max_size) {
                nxweb_send_http_error(resp, 413, "Request Entity Too Large"); // most likely cause
                return NXWEB_ERROR;
            } else if (req->content_received!=fwb->size) {
                nxweb_log_error("Python3: content_received does not match upload stored size for %s", req->uri);
                nxweb_send_http_error(resp, 500, "Internal Server Error");
                return NXWEB_ERROR;
            } else {
                content_fd=fwb->fd;
                if (lseek(content_fd, 0, SEEK_SET)==-1) {
                    nxweb_log_error("Python3: can't lseek() temp upload file for %s", req->uri);
                    nxweb_send_http_error(resp, 500, "Internal Server Error");
                    return NXWEB_ERROR;
                }
            }
        }
    }
    nxweb_log_debug("Invoke Python3");

    PyGILState_STATE gstate=PyGILState_Ensure();
    PyObject *py3_environ = NULL;
    PyObject *py3_func_args = NULL;
    PyObject *py3_result = NULL;
    PyObject *py3_status = NULL;
    PyObject *py3_headers = NULL;
    PyObject *py3_body = NULL;
    PyObject *py3_repr = NULL;
    PyObject *py3_header_tuple = NULL;
    PyObject *py3_name = NULL;
    PyObject *py3_value = NULL;
    py3_environ = PyDict_New();
    PYOBJECT_CHECK(py3_environ, error);
    dict_set(py3_environ, "SERVER_NAME",         PyUnicode_FromStringAndSize(req->host, host_port? (host_port-req->host) : strlen(req->host)));
    dict_set(py3_environ, "SERVER_PORT",         PyUnicode_FromString(host_port? host_port+1 : ""));
    dict_set(py3_environ, "SERVER_PROTOCOL",     PyUnicode_FromString(req->http11? "HTTP/1.1" : "HTTP/1.0"));
    dict_set(py3_environ, "SERVER_SOFTWARE",     PyUnicode_FromString(PACKAGE_STRING));
    dict_set(py3_environ, "GATEWAY_INTERFACE",   PyUnicode_FromString("CGI/1.1"));
    dict_set(py3_environ, "REQUEST_METHOD",      PyUnicode_FromString(req->method));
    dict_set(py3_environ, "REQUEST_URI",         PyUnicode_FromStringAndSize(request_uri, ulen));
    dict_set(py3_environ, "SCRIPT_NAME",         PyUnicode_FromStringAndSize(request_uri, pfxlen));
    dict_set(py3_environ, "PATH_INFO",           PyUnicode_FromStringAndSize(path_info, plen));
    dict_set(py3_environ, "QUERY_STRING",        PyUnicode_FromString(query_string? query_string : ""));
    dict_set(py3_environ, "REMOTE_ADDR",         PyUnicode_FromString(conn->remote_addr));
    dict_set(py3_environ, "CONTENT_TYPE",        PyUnicode_FromString(req->content_type? req->content_type : ""));
    dict_set(py3_environ, "CONTENT_LENGTH",      PyLong_FromLong(req->content_received));
    if (req->cookie)     dict_set(py3_environ, "HTTP_COOKIE",     PyUnicode_FromString(req->cookie));
    if (req->host)       dict_set(py3_environ, "HTTP_HOST",       PyUnicode_FromString(req->host));
    if (req->user_agent) dict_set(py3_environ, "HTTP_USER_AGENT", PyUnicode_FromString(req->user_agent));
    if (req->if_modified_since) {
        struct tm tm;
        gmtime_r(&req->if_modified_since, &tm);
        char ims[32];
        nxweb_format_http_time(ims, &tm);
        dict_set(py3_environ, "HTTP_IF_MODIFIED_SINCE", PyUnicode_FromString(ims));
    }

    if (req->headers) {
        // write added headers
        // encode http headers into CGI variables; see 4.1.18 in https://tools.ietf.org/html/rfc3875
        char hname[256];
        memcpy(hname, "HTTP_", 5);
        char* h=hname+5;
        nx_simple_map_entry* itr;
        for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
            nx_strtoupper(h, itr->name);
            char* p;
            for (p=h; *p; p++) if (*p=='-') *p='_';
            dict_set(py3_environ, hname, PyUnicode_FromString(itr->value));
        }
    }
    dict_set(py3_environ, "wsgi.url_scheme", PyUnicode_FromString(conn->secure? "https" : "http"));
    if (req->content_length) {
        if (content_fd) dict_set(py3_environ, "nxweb.req.content_fd", PyLong_FromLong(content_fd));
        else            dict_set(py3_environ, "nxweb.req.content", PyByteArray_FromStringAndSize(req->content? req->content : "", req->content_received));
    }
    if (req->if_modified_since) dict_set(py3_environ, "nxweb.req.if_modified_since", PyLong_FromLong(req->if_modified_since));
    dict_set(py3_environ, "nxweb.req.uid", PyLong_FromLongLong(req->uid));
    if (req->parent_req) {
        nxweb_http_request* preq=req->parent_req;
        while (preq->parent_req) preq=preq->parent_req; // find root request
        if (preq->uid) dict_set(py3_environ, "nxweb.req.root_uid", PyLong_FromLongLong(preq->uid));
    }

    // ==================== call python ==================== //
    py3_func_args = PyTuple_New(1);
    PYOBJECT_CHECK(py3_func_args, error);
//     fprintf(stderr, "py3_environ  ="); PyObject_Print(py3_environ,  stderr, 0); fprintf(stderr, "\n");
    PyTuple_SetItem(py3_func_args, 0, py3_environ);
    py3_result=PyObject_CallObject(py3_nxweb_on_request_func, py3_func_args);
    PYOBJECT_CHECK(py3_result, error);
//     fprintf(stderr, "py3_result  =");  PyObject_Print(py3_result, stderr, 0); fprintf(stderr, "\n");

    if (PyTuple_Check(py3_result) && PyTuple_Size(py3_result)==3) {
        py3_status =PyTuple_GET_ITEM(py3_result, 0);
        PYOBJECT_CHECK(py3_status, error);
        py3_headers=PyTuple_GET_ITEM(py3_result, 1);
        PYOBJECT_CHECK(py3_headers, error);
        py3_body   =PyTuple_GET_ITEM(py3_result, 2);


        if (PyUnicode_Check(py3_status)) {
            py3_repr = PyObject_Repr(py3_status);
            Py_ssize_t sc1=0;
            char *status_string = PyUnicode_AsUTF8AndSize(py3_repr, &sc1);
            if (sc1>2) { status_string += sc1-1; *status_string = 0; status_string -= sc1-2;} // TODO '
            int status_code=0;
            const char* p=status_string;
            while (*p && *p>='0' && *p<='9') { status_code=status_code*10+(*p-'0'); p++; }
            while (*p && *p==' ') p++;
            if (status_code>=200 && status_code<600 && *p) {
                resp->status_code=status_code;
                resp->status=nxb_copy_str(nxb, p);
            }
        }

        if (PyList_Check(py3_headers)) {
            Py_ssize_t ts1=0, ts2=0;
            int size = PyList_Size(py3_headers);
            for (int i = 0; i < size; i++) {
                py3_header_tuple=PyList_GET_ITEM(py3_headers, i);
                PYOBJECT_CHECK(py3_header_tuple, error);
                if (PyTuple_Check(py3_header_tuple) && PyTuple_Size(py3_header_tuple)==2) {
                    py3_name  = PyObject_Repr(PyTuple_GET_ITEM(py3_header_tuple, 0));
                    PYOBJECT_CHECK(py3_name, error);
                    py3_value = PyObject_Repr(PyTuple_GET_ITEM(py3_header_tuple, 1));
                    PYOBJECT_CHECK(py3_value, error);
                    char *py3_name_c  = PyUnicode_AsUTF8AndSize(py3_name,  &ts1);
                    if (ts1>2) { py3_name_c += ts1-1; *py3_name_c = 0; py3_name_c -= ts1-2;} // TODO '
                    char *py3_value_c = PyUnicode_AsUTF8AndSize(py3_value, &ts2);
                    if (ts2>2) { py3_value_c += ts2-1; *py3_value_c = 0; py3_value_c -= ts2-2;} // TODO '
                    nxweb_add_response_header_safe(resp, py3_name_c, py3_value_c);
                }
            }
        }

        if (py3_body) {
            char *rcontent=0;
            nxe_ssize_t rsize=0;
            if  (PyByteArray_Check(py3_body))   { rcontent=PyByteArray_AS_STRING(py3_body); rsize=PyByteArray_Size(py3_body); }
            else if (PyBytes_Check(py3_body))   { rcontent=PyBytes_AsString(py3_body);      rsize=PyBytes_GET_SIZE(py3_body); }
            else if (PyUnicode_Check(py3_body)) { rcontent=PyBytes_AS_STRING(py3_body);     rsize=PyUnicode_GET_SIZE(py3_body); }
            if ((!resp->status_code || resp->status_code==200) && !resp->content_type) resp->content_type="text/html";
            if (rcontent && rsize>0) { nxweb_response_append_data(resp, rcontent, rsize); }
        }
    } else if (PyUnicode_Check(py3_result)) {
        resp->status_code=500;
        resp->status="Internal Server Error";
        resp->content_type="text/html";
        nxweb_log_error("Python3: call failed: %s", PyBytes_AS_STRING(py3_result));
        nxweb_response_printf(resp, "script call failed: %H", PyBytes_AS_STRING(py3_result));
    } else {
        PyErr_Print();
        nxweb_log_error("Python3: call failed");
        nxweb_response_printf(resp, "script call failed");
    }
error:
    if (py3_repr)         Py_XDECREF(py3_repr);
    if (py3_header_tuple) Py_XDECREF(py3_header_tuple);
    if (py3_name)         Py_XDECREF(py3_name);
    if (py3_value)        Py_XDECREF(py3_value);
    if (py3_environ)      Py_XDECREF(py3_environ);
    if (py3_func_args)    Py_XDECREF(py3_func_args);
    if (py3_result)       Py_XDECREF(py3_result);
    PyGILState_Release(gstate);
    nxweb_log_debug("Invoke Python3 complete!");
    return NXWEB_OK;
}

static nxweb_result python3_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
    if (!py3_module) return NXWEB_NEXT; // skip if python not initialized
    return NXWEB_OK;
}

static nxweb_result python3_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
    if (!py3_module) return NXWEB_NEXT; // skip if python not initialized
    if (!req->get_method || req->content_length) return NXWEB_OK; // do not cache POST requests, etc.
    nxb_start_stream(req->nxb);
    _nxb_append_encode_file_path(req->nxb, req->host);
    if (conn->secure) nxb_append_str(req->nxb, "_s");
    _nxb_append_encode_file_path(req->nxb, req->uri);
    nxb_append_char(req->nxb, '\0');
    resp->cache_key=nxb_finish_stream(req->nxb, 0);
    return NXWEB_OK;
}


NXWEB_MODULE(python3,
    .on_server_startup  = on_startup,
    .on_server_shutdown = on_shutdown,
    .on_config          = on_config
);

NXWEB_DEFINE_HANDLER(python3,
    .on_select              = python3_on_select,
    .on_request             = python3_on_request,
    .on_generate_cache_key  = python3_generate_cache_key,
    .on_post_data           = python3_on_post_data,
    .on_post_data_complete  = python3_on_post_data_complete,
    .flags                  = NXWEB_HANDLE_ANY|NXWEB_INWORKER
);
