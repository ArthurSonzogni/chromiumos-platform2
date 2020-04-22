#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Standalone local webserver to acquire fingerprints for user studies."""

from __future__ import print_function

import argparse
from datetime import datetime
import json
import os
import re
import subprocess
import sys
import threading
import time

import cherrypy
from ws4py.server.cherrypyserver import WebSocketPlugin, WebSocketTool
from ws4py.websocket import WebSocket

# use the image conversion library if available
sys.path.extend(['/usr/local/opt/fpc', '/opt/fpc'])
try:
  import fputils
except ImportError:
  fputils = None

errors = [
    # FP_SENSOR_LOW_IMAGE_QUALITY 1
    'retrying...',
    # FP_SENSOR_TOO_FAST 2
    'keeping your finger still during capture',
    # FP_SENSOR_LOW_SENSOR_COVERAGE 3
    'centering your finger on the sensor',
]

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
HTML_DIR = os.path.join(SCRIPT_DIR, 'html')

ECTOOL = 'ectool'
# Wait to see a finger on the sensor
FP_MODE_FINGER_DOWN = 2
# Poll until the finger has left the sensor
FP_MODE_FINGER_UP = 4
# Capture the current finger image
FP_MODE_CAPTURE = 8

class FingerWebSocket(WebSocket):
  """Handle the websocket used finger images acquisition and logging."""

  FP_MODE_RE = re.compile(r'^FP mode:\s*\(0x([0-9a-fA-F]+)\)', re.MULTILINE)
  DIR_FORMAT = '{participant:04d}/{group:s}/{finger:02d}'
  FILE_FORMAT = '{finger:02d}_{picture:02d}'

  config = {}
  pict_dir = '/tmp'
  # FpUtils class to process images through the external library.
  utils = None
  # The worker thread processing the images.
  worker = None
  # The current request processed by the worker thread.
  current_req = None
  # The Condition variable the worker thread is waiting on to get a new request.
  available_req = threading.Condition()
  # Force terminating the current processing in the worker thread.
  abort_request = False

  def set_config(self, arg):
    self.config = {
        'fingerCount' : arg.finger_count,
        'enrollmentCount' : arg.enrollment_count,
        'verificationCount': arg.verification_count
    }
    self.pict_dir = arg.picture_dir
    if fputils:
      self.utils = fputils.FpUtils()
    self.worker = threading.Thread(target=self.finger_worker)
    self.worker.start()

  def closed(self, code, reason=''):
    self.abort_request = True
    cherrypy.log('Websocket closed with code %d / %s' % (code, reason))
    if not self.worker:
      cherrypy.log("Worker thread wasn't running.")
      return
    cherrypy.log('Stopping worker thread.')
    # Wake up the thread so it can exit.
    self.available_req.acquire()
    self.available_req.notify()
    self.available_req.release()
    self.worker.join(10.0)
    if self.worker.isAlive():
      cherrypy.log('Failed to stop worker thread.')
    else:
      cherrypy.log('Successfully stopped worker thread.')

  def received_message(self, m):
    if m.is_binary:
      return # Not supported
    j = json.loads(m.data)
    if 'log' in j:
      cherrypy.log(j['log'])
    if 'finger' in j:
      self.finger_request(j)
    if 'config' in j:
      self.config_request(j)

  def make_dirs(self, path):
    # Ensure the image directory exists
    if not os.path.exists(path):
      os.makedirs(path)

  def ectool(self, command: str, *params) -> bytes:
    """Run the ectool command and return its stdout as bytes"""

    cmdline = [ECTOOL, '--name=cros_fp', command] + list(params)
    stdout = b''
    while not self.abort_request:
      try:
        stdout = subprocess.check_output(cmdline)
        break
      except subprocess.CalledProcessError as e:
        cherrypy.log("command '%s' failed with %d" % (e.cmd, e.returncode))
        stdout = b''
        # try again
    return stdout

  def ectool_fpmode(self, *params) -> int:
    mode = self.ectool('fpmode', *params).decode('utf-8')
    match_mode = self.FP_MODE_RE.search(mode)
    return int(match_mode.group(1), 16) if match_mode else -1

  def finger_wait_done(self, mode):
    # Poll until the mode bit has disappeared
    while not self.abort_request and self.ectool_fpmode() & mode:
      time.sleep(0.050)
    return not self.abort_request

  def finger_save_image(self, req):
    directory = os.path.join(self.pict_dir, self.DIR_FORMAT.format(**req))
    self.make_dirs(directory)
    file_base = os.path.join(directory, self.FILE_FORMAT.format(**req))
    raw_file = file_base + '.raw'
    fmi_file = file_base + '.fmi'
    img = self.ectool('fpframe', 'raw')
    if not img:
      return
    cherrypy.log("Saving file '%s' size %d" % (raw_file, len(img)))
    with open(raw_file, 'wb') as f:
      f.write(img)
    if self.utils:
      rc, fmi = self.utils.image_data_to_fmi(img)
      if rc == 0:
        with open(fmi_file, 'wb') as f:
          f.write(fmi)
      else:
        cherrypy.log('FMI conversion failed %d' % (rc))

  def finger_process(self, req):
    # Ensure the user has removed the finger between 2 captures
    if not self.finger_wait_done(FP_MODE_FINGER_UP):
      return
    # Capture the finger image when the finger is on the sensor
    self.ectool_fpmode('capture', 'vendor')
    t0 = time.time()
    # Wait for the image being available
    if not self.finger_wait_done(FP_MODE_CAPTURE):
      return
    t1 = time.time()
    # detect the finger removal before the next capture
    self.ectool_fpmode('fingerup')
    # record the outcome of the capture
    cherrypy.log('Captured finger %02d:%02d in %.2fs' % (req['finger'],
                                                         req['picture'],
                                                         t1 - t0))
    req['result'] = 'ok' # ODER req['result'] = errors[ERRNUM_TBD]
    # retrieve the finger image
    self.finger_save_image(req)
    # tell the page about the acquisition result
    self.send(json.dumps(req), False)

  def finger_worker(self):
    while not self.client_terminated:
      self.available_req.acquire()
      while not self.current_req and not self.abort_request:
        self.available_req.wait()
      self.finger_process(self.current_req)
      self.current_req = None
      self.available_req.release()

  def finger_request(self, req):
    # ask the thread to exit the waiting loops
    # it will wait on the acquire() below if needed
    self.abort_request = True
    # ask the thread to process the new request
    self.available_req.acquire()
    self.abort_request = False
    self.current_req = req
    self.available_req.notify()
    self.available_req.release()

  def config_request(self, req):
    # Populate configuration.
    req['config'] = self.config
    self.send(json.dumps(req), False)

class Root(object):
  """Serve the static HTML/CSS and connect the websocket."""

  def __init__(self, cmdline_args):
    self.args = cmdline_args

  @cherrypy.expose
  def index(self):
    with open(os.path.join(SCRIPT_DIR, 'html/index.html')) as f:
      return f.read()

  @cherrypy.expose
  def finger(self):
    cherrypy.request.ws_handler.set_config(args)

if __name__ == '__main__':
  # Get study parameters from the command-line
  parser = argparse.ArgumentParser()
  parser.add_argument('-f', '--finger_count', type=int, default=2,
                      help='Number of fingers acquired per user')
  parser.add_argument('-e', '--enrollment_count', type=int, default=20,
                      help='Number of enrollment images per finger')
  parser.add_argument('-v', '--verification_count', type=int, default=15,
                      help='Number of verification images per finger')
  parser.add_argument('-p', '--port', type=int, default=9000,
                      help='port for the webserver socket')
  parser.add_argument('-d', '--picture_dir', default='./fingers',
                      help='Log files directory')
  parser.add_argument('-l', '--log_dir')
  args = parser.parse_args()
  # Configure cherrypy server
  cherrypy.config.update({'server.socket_port': args.port})
  if args.log_dir:
    log_name = 'server-%s.log' % (datetime.now().strftime('%Y%m%d_%H%M%S'))
    cherrypy.config.update({
        'log.access_file' : os.path.join(args.log_dir, 'access.log'),
        'log.error_file' : os.path.join(args.log_dir, log_name),
        'log.screen' : False})
  WebSocketPlugin(cherrypy.engine).subscribe()
  cherrypy.tools.websocket = WebSocketTool()

  cherrypy.quickstart(Root(args), '/', config={
      '/finger': {'tools.websocket.on': True,
                  'tools.websocket.handler_cls': FingerWebSocket},
      '/static': {'tools.staticdir.on': True,
                  'tools.staticdir.dir': HTML_DIR}})
