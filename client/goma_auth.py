#!/usr/bin/env python

# Copyright 2015 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A Script to set goma_oauth2_config."""

import argparse
import BaseHTTPServer
import copy
import json
import os
import string
import subprocess
import sys
import urllib
import urlparse
import webbrowser
import random

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
GOOGLE_AUTH_URI = 'https://accounts.google.com/o/oauth2/auth'
OAUTH_SCOPES = 'https://www.googleapis.com/auth/userinfo.email'
OAUTH_TOKEN_ENDPOINT = 'https://www.googleapis.com/oauth2/v3/token'
TOKEN_INFO_ENDPOINT = 'https://www.googleapis.com/oauth2/v3/tokeninfo'
OOB_CALLBACK_URN = 'urn:ietf:wg:oauth:2.0:oob'

GOMA_SETTINGS_URI = 'https://cxx-compiler-service.appspot.com/settings'

DEFAULT_GOMA_OAUTH2_CONFIG_FILE_NAME = '.goma_client_oauth2_config'

OAUTH_STATE_LENGTH = 64

if os.name == 'nt':
  GOMA_FETCH = os.path.join(SCRIPT_DIR, 'goma_fetch.exe')
else:
  GOMA_FETCH = os.path.join(SCRIPT_DIR, 'goma_fetch')



def ConfirmUserAgreedToS():
  """Returns if user agreed on ToS."""
  print """You can use Goma (distributed compiler service) if you agree on the
way we use data and the way we share data.

1. How do we use data?
a. the data used for compiling
- source code (including contents and file paths) to be compiled.
- header files (including contents and file paths) used during the compile.
- other files accessed by compilers. e.g. asan_blacklist.txt, crtbegin.o,
  profiling data for pgo.
- identifier of a compiler to use. (SHA256 hash value of a compiler, version,
  target).
- command line arguments and environment variables necessary for a compile,
  system include paths, current working directory.

b. the data used for authentication
- OAuth2 access token to use service, and email address gotten from access
  token.

Google may use data for logging and tracking (including abuse detection).
Google keeps identifier of each compile (goma client start time, goma client
id that changes when compiler_proxy starts, sequential compile id)

2. What data will be shared?

Contents in source code and header files are stored in Google servers and
shared among users who send SHA256 hash values of them.  Compile results
are shared among users who have sent the requests that bring the same
compile result.

3. How long data will be kept?

Contents in source code and header files will be kept at most 153 days
since last used.
"""

  yn = raw_input('Do you agree to our data usage policy? (y/n) -->')
  if yn in ('Y', 'y'):
    print 'You have agreed.'
    return
  sys.exit(1)


class Error(Exception):
  """Raised on Error."""


class GomaOAuth2Config(dict):
  """File-backed OAuth2 configuration."""

  def __init__(self):
    dict.__init__(self)
    self._path = self._GetLocation()

  @staticmethod
  def _GetLocation():
    """Returns Goma OAuth2 config file path."""
    env_name = 'GOMA_OAUTH2_CONFIG_FILE'
    env = os.environ.get(env_name)
    if env:
      return env
    homedir = os.path.expanduser('~')
    if homedir == '~':
      raise Error('Cannot find user\'s home directory.')
    return os.path.join(homedir, DEFAULT_GOMA_OAUTH2_CONFIG_FILE_NAME)

  def Load(self):
    """Loads config from a file."""
    if not os.path.exists(self._path):
      return False
    try:
      with open(self._path) as f:
        self.update(json.load(f))
    except ValueError:
      return False
    if not self.get('refresh_token'):
      return False
    return True

  def Save(self):
    """Saves config to a file."""
    # TODO: not save unnecessary data.
    with open(self._path, 'wb') as f:
      if os.name == 'posix':
        os.fchmod(f.fileno(), 0600)
      json.dump(self, f)

  def Delete(self):
    """Deletes a config file."""
    if not os.path.exists(self._path):
      return
    os.remove(self._path)


def HttpGetRequest(url):
  """Proceed an HTTP GET request, and returns an HTTP response body.

  Args:
    url: a URL string of an HTTP server.

  Returns:
    a response from the server.
  """
  cmd = [GOMA_FETCH, '--noauth', url]
  return subprocess.check_output(cmd)


def HttpPostRequest(url, post_dict):
  """Proceed an HTTP POST request, and returns an HTTP response body.

  Args:
    url: a URL string of an HTTP server.
    post_dict: a dictionary of a body to be posted.

  Returns:
    a response from the server.
  """
  body = urllib.urlencode(post_dict)
  cmd = [GOMA_FETCH, '--noauth', '--post', url, '--data', body]
  return subprocess.check_output(cmd)


def DefaultOAuth2Config():
  """Returns default OAuth2 config.

  same as oauth2.cc:DefaultOAuth2Config.
  TODO: run compiler_propxy to generate default oauth2 config?

  Returns:
    a dictionary of OAuth2 config.
  """
  return {
      'client_id': ('687418631491-r6m1c3pr0lth5atp4ie07f03ae8omefc.'
                    'apps.googleusercontent.com'),
      'client_secret': 'R7e-JO3L5sKVczuR-dKQrijF',
      'redirect_uri': OOB_CALLBACK_URN,
      'auth_uri': GOOGLE_AUTH_URI,
      'scope': OAUTH_SCOPES,
      'token_uri': OAUTH_TOKEN_ENDPOINT,
  }


class AuthorizationCodeHandler(BaseHTTPServer.BaseHTTPRequestHandler):
  """HTTP handler to get authorization code."""

  code = None
  state = None

  @classmethod
  def _SetCode(cls, code):
    """Internal function to set code to class variable."""
    if not code:
      raise Error('code is None')
    cls.code = code[0]

  def do_GET(self):
    """A handler to receive authorization code."""
    if self.client_address[0] not in ('127.0.0.1', '::1'):
      raise Error('should be localhost but %s' % self.client_address[0])
    form = urlparse.parse_qs(urlparse.urlparse(self.path).query)
    server_state = form.get('state', [''])[0]
    if server_state != self.state:
      raise Error('possibly XSRF: state from server (%s) is not %s' % (
          server_state, self.state))
    self._SetCode(form.get('code'))
    self.send_response(200, "OK")
    self.send_header('Content-type', 'text/html')
    self.end_headers()
    self.wfile.write('<html><head><title>Authentication Status</title></head>')
    self.wfile.write('<body><p>The authentication flow has completed.</p>')
    self.wfile.write('</body></html>')

  def log_message(self, _format, *args):
    """Do not log messages to stdout while running as command line program."""


def _RandomString(length):
  """Returns random string.

  Args:
    length: length of the string.

  Returns:
    random string.
  """
  generator = random.SystemRandom()
  return ''.join(generator.choice(string.letters + string.digits)
                 for _ in xrange(length))


def GetAuthorizationCodeViaBrowser(config):
  """Gets authorization code using browser.

  This way is useful for users with desktop machines.

  Args:
    config: a dictionary of config.

  Returns:
    authorization code.
  """
  AuthorizationCodeHandler.state = _RandomString(OAUTH_STATE_LENGTH)
  httpd = BaseHTTPServer.HTTPServer(('', 0), AuthorizationCodeHandler)
  config['redirect_uri'] = 'http://localhost:%d' % httpd.server_port
  body = urllib.urlencode({
      'scope': config['scope'],
      'redirect_uri': config['redirect_uri'],
      'client_id': config['client_id'],
      'state': AuthorizationCodeHandler.state,
      'response_type': 'code'})
  google_auth_url = '%s?%s' % (config['auth_uri'], body)
  webbrowser.open(google_auth_url)
  httpd.handle_request()
  httpd.server_close()
  return AuthorizationCodeHandler.code


def GetAuthorizationCodeViaCommandLine(config):
  """Gets authorization code via command line.

  This way is useful anywhere without a browser.

  Args:
    config: a dictionary of config.

  Returns:
    authorization code.
  """
  body = urllib.urlencode({
      'scope': config['scope'],
      'redirect_uri': config['redirect_uri'],
      'client_id': config['client_id'],
      'response_type': 'code'})
  google_auth_url = '%s?%s' % (config['auth_uri'], body)
  print 'Please visit following URL with your browser, and approve access:'
  print google_auth_url
  return raw_input('Please input the code:')


def GetRefreshToken(get_code_func, config):
  """Get refresh token with oauth 3 legged authentication.

  Args:
    get_code_func: a function for getting authorization code.
    config: a dictionary of config.

  Returns:
    a refresh token string.
  """
  code = get_code_func(config)
  assert code and isinstance(code, str)
  post_data = {
      'code': code,
      'client_id': config['client_id'],
      'client_secret': config['client_secret'],
      'redirect_uri': config['redirect_uri'],
      'grant_type': 'authorization_code'
  }
  resp = json.loads(HttpPostRequest(config['token_uri'], post_data))
  return resp['refresh_token']


def VerifyRefreshToken(config):
  """Verify refresh token in config.

  Returns:
     '' if a refresh token in config is valid.
     error message if something wrong.
  """
  if not 'refresh_token' in config:
    return 'no refresh token in config'
  post_data = {
      'client_id': config['client_id'],
      'client_secret': config['client_secret'],
      'refresh_token': config['refresh_token'],
      'grant_type': 'refresh_token'
  }
  resp = json.loads(HttpPostRequest(config['token_uri'], post_data))
  if 'error' in resp:
    return 'obtain access token: %s' % resp['error']
  token_info = json.loads(HttpPostRequest(
      TOKEN_INFO_ENDPOINT,
      {'access_token': resp['access_token']}))
  if 'error_description' in token_info:
    return 'token info: %s' % token_info['error_description']
  if not 'email' in token_info:
    return 'no email in token_info %s' % token_info
  print 'Login as ' + token_info['email']
  return ''


def CheckSettings():
  """Check settings with current login user.

  Returns:
    true if user is ready to use Goma.
  """
  cmd = [GOMA_FETCH, GOMA_SETTINGS_URI]
  try:
    subprocess.check_output(cmd, stderr=subprocess.STDOUT)  # discard outputs.
    print 'Ready to use Goma'
    return True
  except subprocess.CalledProcessError:
    print 'Current user is not registered with Goma service. Unable to use Goma'
  return False


def Login():
  """Performs interactive login and caches authentication token.

  Returns:
    non-zero value on error.
  """
  ConfirmUserAgreedToS()

  parser = argparse.ArgumentParser()
  parser.add_argument('--browser', action='store_true',
                      help=('Use browser to get goma OAuth2 token.'))
  options = parser.parse_args(sys.argv[2:])

  config = GomaOAuth2Config()
  config.update(DefaultOAuth2Config())
  func = GetAuthorizationCodeViaCommandLine
  if options.browser:
    func = GetAuthorizationCodeViaBrowser
  config['refresh_token'] = GetRefreshToken(func, config)

  err = VerifyRefreshToken(config)
  if err:
    sys.stderr.write(err + '\n')
    return 1

  config.Save()
  if not CheckSettings():
    return 1
  return 0


def Logout():
  """Removes a cached authentication token.

  Returns:
    non-zero value on error.
  """
  config = GomaOAuth2Config()
  config.Delete()
  return 0


def Info():
  """Shows email associated with a cached authentication token.

  Returns:
    non-zero value on error.
  """
  config = GomaOAuth2Config()
  if not config.Load():
    sys.stderr.write('Not logged in\n')
    return 1
  err = VerifyRefreshToken(config)
  if err:
    sys.stderr.write(err + '\n')
    return 1
  if not CheckSettings():
    return 1
  return 0


def Help():
  """Print Usage"""
  print '''Usage: %(cmd)s <command> [options]

Commands are:
  login  performs interactive login and caches authentication token
  logout revokes cached authentication token
  info   shows email associated with a cached authentication token

Options are:
  --browser use browser to get goma OAuth2 token (login command only)
''' % {'cmd' : sys.argv[0]}
  return 0


def main():
  action_mapping = {
      'login': Login,
      'logout': Logout,
      'info': Info,
  }
  action = Help
  if len(sys.argv) > 1:
    action = action_mapping.get(sys.argv[1], Help)
  return action()


if __name__ == '__main__':
  sys.exit(main())
