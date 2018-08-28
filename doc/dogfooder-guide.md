# Goma dogfooding for Chromium contributors

This document describes how to install and use Goma.  It is intended for
external Chromium contributors.

[TOC]

# Getting started

This section explains the one-shot procedure to start using the Goma service.
You need to do the following once you start using Goma.

## System requirements

* A x86-64 machine with at least 8GB of RAM.
* You must have python 2.7 installed already.
* You must have [depot\_tools](https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md#install) installed already.

Most development is done on Ubuntu (currently 14.04, Trusty Tahr).
There are some instructions for other distros below, but they are mostly
unsupported.

## Please wait for invitation from Google

*You can use the Goma service upon invitation from Google*

We will gradually invite active external Chromium contributors to dogfood
the Goma service (i.e. using Goma backend server paid for by Google).

The rest of the procedures will work after you are registered as a dogfooder.

## Download prebuilt Goma client

<!-- TODO: make the automated script available instead. -->

```shell
$ cipd install infra/goma/client/linux-amd64 -root ${HOME}/goma
```

Note:
the package with ref=latest is chosen by default. However, we may use other
cipd ref, and provide the script to automate Goma installation and update in
the future.

## Login to Goma service

Please use `goma_auth.py` to login. To use Goma service, you must agree
to our data usage policy.

```shell
$ ${HOME}/goma/goma_auth.py login
```

# How to build Chromium with Goma?

This section explains the typical workflow for using Goma.

## Start `compiler_proxy` daemon

```shell
$ ${HOME}/goma/goma_ctl.py ensure_start
```

## Build chromium using Goma

```shell
$ cd ${chromium_src}
$ gn gen out/Default --args='use_goma=true'
```
Or, enable Goma by setting the GN arg `use_goma=true`.

Then, build chromium using Goma.

```shell
$ cd ${chromium_src}
$ autoninja -C out/Default
```

# FAQ

## How to use the latest Goma client?

If there is a new Goma client commit, the prebuilt Goma client in cipd
repository will automatically be updated.
Please make sure no program on your machine is using Goma. i.e. building
chromium.  Then, stop `compiler_proxy`, update Goma client, and restart
`compiler_proxy`.

```shell
$ ${HOME}/goma/goma_ctl.py ensure_stop
$ echo 'infra/goma/client/linux-amd64 latest' | \
  cipd ensure -ensure-file - -root ${HOME}/goma
$ ${HOME}/goma/goma_ctl.py ensure_start
```

# Troubleshooting

## `goma_ctl.py ensure_start` shows `error: failed to connect to backend servers`

1. Please make sure you have logged in with an email address that you have
   registered.

   The following command shows which user is currently logged in. Please confirm
   that the email address shown is the same one that you had used to nominate
   yourself as a dogfooder.

   ```shell
   $ ${HOME}/goma_auth.py info
   ```

1. Plese make sure you are registered as a dogfooder.

   TODO: write this.  We do not have available backend now.

1. Please make sure you do not set `GOMA_*` environment by yourself.

   The following command should not show anything.

   ```shell
   $ env | grep ^GOMA_
   ```

If all of above did not help, please file an issue in crbug.com
with `Infra>Goma` component.
