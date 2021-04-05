mod_realdoc is an Apache module which does a realpath on
the docroot symlink and sets the absolute path as the
real document root for the remainder of the request.

It executes as soon as Apache is finished reading the request
from the client.

The realpath frequency can be adjusted in httpd.conf using:

    <IfModule mod_realdoc.c>
        RealpathEvery 2
    </IfModule>

By resolving the configured symlinked docroot directory to
an absolute path at the start of a request we can safely
switch this symlink to point to another directory on a
deploy. Requests that started before the symlink change will
continue to execute on the previous symlink target and
therefore will not be vulnerable to deploy race conditions.

This module is intended for the prefork mpm. Threaded mpms
will incur race conditions.

Compile and install with:

    apxs -c mod_realdoc.c
    sudo apxs -i -a -n realdoc mod_realdoc.la

or just

    make install

Please note that if you're granting access to your document root
using a symlink, that will stop working, unless expanding the
symlink also in your Apache configuration.
￼
If, for instance, your symlink is named "current" and your releases
are something like "releases/20160519102200" (with "current" pointing
to last release), you need to adapt your configration.
￼
```apacheconf
# Old configuration (without mod_realdoc):
<Directory "/var/www/your_host/current">
    Require all granted
</Directory>

# New configuration (with mod_realdoc):
<Directory ~ "/var/www/your_host/releases/\d{14}">
    Require all granted
</Directory>
```
You need to adapt the regular expression `\d{14}` if you use a
different schema from timestamp in your releases.

If you want to map incremental releases to two static docroot
symlinks in order to re-use your opcache cache entries, you can
set

```apacheconf
UseReadlink On
```

This means that instead of calling `realpath()` it will call `readlink()`
on the first symlink it finds in your configured docroot path. This means
you can do:

```
                                  /var/www/release-11
/var/www/current -> /var/www/A -> /var/www/release-12
                    /var/www/B -> /var/www/release-13
```

Then when release-13 is ready to go live, flip the symlink to B. For PHP you
are going to need the [resolve_symlinks](https://wiki.php.net/rfc/resolve_symlinks) patch
in order to only have `/var/www/A` and `/var/www/B` opcache entries. Without that
patch PHP will call `realpath()` and you will have `/var/www/release-*` cache entries
which means no cache re-use.

There is also a small optimization. Instead of just turning it on, you can
tell it where to start checking for symlinks from in your docroot path like
this:

```apacheconf
UseReadlink /var/www/current
```

this saves a couple of `lstat()` syscalls.
