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


GOOGLE_AUTH_URI = 'https://accounts.google.com/o/oauth2/auth'
OAUTH_SCOPES = 'https://www.googleapis.com/auth/userinfo.email'
OAUTH_TOKEN_ENDPOINT = 'https://www.googleapis.com/oauth2/v3/token'
OOB_CALLBACK_URN = 'urn:ietf:wg:oauth:2.0:oob'
GOMA_OAUTH_SCOPE='https://www.googleapis.com/auth/goma'

CHROME_INFRA_CONFIG_URL = ('https://chrome-infra-auth.appspot.com/'
                           'auth/api/v1/server/oauth_config')
CHROME_INFRA_CONFIG_DEV_URL = ('https://chrome-infra-auth-dev.appspot.com/'
                               'auth/api/v1/server/oauth_config')
DEFAULT_GOMA_OAUTH2_CONFIG_FILE_NAME = '.goma_oauth2_config'

OAUTH_STATE_LENGTH = 64


class Error(Exception):
  """Raised on Error."""


class GomaOAuth2Config(dict):
  """File-backed OAuth2 configuration."""

  def __init__(self):
    dict.__init__(self)
    self._path = self._GetLocation()
    self._backup = None

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
    self._backup = copy.copy(self)
    return True

  def Save(self):
    """Saves config to a file."""
    # TODO: not save unnecessary data.
    if self == self._backup:
      return
    with open(self._path, 'wb') as f:
      if os.name == 'posix':
        os.fchmod(f.fileno(), 0600)
      json.dump(self, f)
    self._backup = copy.copy(self)

  def Delete(self):
    """Deletes a config file."""
    if not os.path.exists(self._path):
      return
    os.remove(self._path)
    self._backup = None


def HttpGetRequest(url):
  """Proceed an HTTP GET request, and returns an HTTP response body.

  Note: using curl instead of urllib2.urlopen because python < 2.7.9 does
  not verify certificates. See http://lwn.net/Articles/611243/

  Args:
    url: a URL string of an HTTP server.

  Returns:
    a response from the server.
  """
  cmd = ['curl', url, '--silent', '-o', '-']
  return subprocess.check_output(cmd)


def HttpPostRequest(url, post_dict):
  """Proceed an HTTP POST request, and returns an HTTP response body.

  Note: using curl instead of urllib2.urlopen because python < 2.7.9 does
  not verify certificates. See http://lwn.net/Articles/611243/

  Args:
    url: a URL string of an HTTP server.
    post_dict: a dictionary of a body to be posted.

  Returns:
    a response from the server.
  """
  body = urllib.urlencode(post_dict)
  cmd = ['curl', '-d', body, url, '--silent', '-o', '-']
  return subprocess.check_output(cmd)


def OAuth2BasicConfig():
  """Returns a dictionary of a basic OAuth2 config."""
  return {
      'client_id': '',
      'client_secret': '',
      'redirect_uri': OOB_CALLBACK_URN,
      'auth_uri': GOOGLE_AUTH_URI,
      'scope': OAUTH_SCOPES,
      'token_uri': OAUTH_TOKEN_ENDPOINT,
  }


def ReadOAuth2ConfigFromSite(is_dev):
  """Returns OAuth2 config that is come from chrome-infra-site.

  Args:
    is_dev: use dev site on True.

  Returns:
    a dictionary of OAuth2 config.
  """
  url = CHROME_INFRA_CONFIG_DEV_URL if is_dev else CHROME_INFRA_CONFIG_URL
  config = json.loads(HttpGetRequest(url))
  ret = OAuth2BasicConfig()
  ret.update({
      'client_id': config['client_id'],
      'client_secret': config['client_not_so_secret'],
  })
  return ret


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
    if self.address_string() != 'localhost':
      raise Error('should be localhost but %s' % self.client_address)
    form = urlparse.parse_qs(urlparse.urlparse(self.path).query)
    server_state = form.get('state', [''])[0]
    if server_state != self.state:
      raise Error('possibly XSRF: state from server (%s) is not %s' % (
          server_state, self.state))
    self._SetCode(form.get('code'))
    self.send_response(200, "OK")


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
  assert code and type(code) == str
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
  """Returns True if a refresh token in config is valid."""
  if not 'refresh_token' in config:
    return False
  post_data = {
      'client_id': config['client_id'],
      'client_secret': config['client_secret'],
      'refresh_token': config['refresh_token'],
      'grant_type': 'refresh_token'
  }
  resp = json.loads(HttpPostRequest(config['token_uri'], post_data))
  if 'error' in resp:
    return False
  return 'access_token' in resp


def Login(options):
  """Does login procedure.

  If there is valid config, it does nothing.
  If config is invalid, raise.
  If there is no config, it asks the user to get refresh token.
  """
  config = GomaOAuth2Config()
  if options.delete:
    config.Delete()
  if not config.Load():
    config.update(ReadOAuth2ConfigFromSite(options.dev_chrome_infra_site))
    func = GetAuthorizationCodeViaCommandLine
    if options.browser:
      func = GetAuthorizationCodeViaBrowser
    config['refresh_token'] = GetRefreshToken(func, config)

  if not VerifyRefreshToken(config):
    raise Error('invalid refresh token')

  config.Save()


def main():
  print '!!!EXPERIMENTAL!!!  WE MAY CHANGE THIS WITHOUT ANNOUNCEMENT.'
  parser = argparse.ArgumentParser()
  parser.add_argument('--dev_chrome_infra_site', action='store_true',
                      help=('Use dev chrome infra site.'))
  parser.add_argument('--delete', action='store_true',
                      help=('Delete the stored goma OAuth2 config file.'))
  parser.add_argument('--browser', action='store_true',
                      help=('Use browser to get goma OAuth2 token.'))
  options = parser.parse_args()
  Login(options)


if __name__ == '__main__':
  sys.exit(main())
