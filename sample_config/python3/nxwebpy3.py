#!/usr/bin/python3
#-*- coding:utf-8 -*-

import os
os.environ['PYTHONUNBUFFERED'] = '1'
os.environ['PYTHONIOENCODING'] = 'UTF-8'

import sys
sys.stdout.flush()
sys.stderr.flush()

import io, traceback

WSGI_APP=None

if len(sys.argv)>=4 and sys.argv[3]:
  activate_this=os.path.abspath(os.path.join(sys.argv[3], 'bin/activate_this.py'))
  if os.path.isfile(activate_this):
    execfile(activate_this, dict(__file__=activate_this))
  else:
    print('python config error: cannot activate virtualenv', activate_this)

if len(sys.argv)>=3:
  if sys.argv[1]:
    project_path=os.path.abspath(sys.argv[1])
    sys.path.append(project_path)
    print('python project path:', project_path)
  project_app=sys.argv[2]
  if project_app:
    p=project_app.rsplit('.', 1)
    if len(p)==2:
      module_name, app_name=p
      try:
        mod=__import__(module_name, globals(), locals(), [app_name])
        if mod and hasattr(mod, app_name):
          WSGI_APP=getattr(mod, app_name)
        else:
          print('python config error: there is no', app_name, 'in module', module_name)
      except:
        traceback.print_exc()
        print('python config error: failed to import module', module_name)
    else:
      print('python config error: wsgi_application is expected in form <module>.<app>')

if not WSGI_APP:
  raise Exception('python config error: no WSGI_APP defined')



def _call_wsgi_application(app, environ):
    body_writer=io.BytesIO()
    status_headers=[None, None]
    def start_response(status, headers, exc_info=None):
        status_headers[:]=[status, headers]
        return body_writer
    app_iter=app(environ, start_response)
    try:
        for item in app_iter:
            body_writer.write(item)
            #.encode()
    finally:
        if hasattr(app_iter, 'close'): app_iter.close()
    return status_headers[0], status_headers[1], body_writer.getvalue()


def _nxweb_on_request(environ):
    try:
        environ['wsgi.version']=(1, 0)
        if 'nxweb.req.content' in environ:
            environ['wsgi.input']=io.BytesIO(environ['nxweb.req.content'])
        elif 'nxweb.req.content_fd' in environ:
            environ['wsgi.input']=os.fdopen(environ['nxweb.req.content_fd'], 'r')
        else:
            environ['wsgi.input']=None

        environ['wsgi.errors']=sys.stderr
        environ['wsgi.multithread']=True
        environ['wsgi.multiprocess']=False
        environ['wsgi.run_once']=False
        #print (str(environ).replace(',',',\n'))
        return _call_wsgi_application(WSGI_APP, environ)
    except:
        ei=sys.exc_info()
        traceback.print_exception(*ei)
        return repr(ei[1])+' see log for details'


