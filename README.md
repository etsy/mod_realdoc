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
￼If, for instance, your symlink is named "current" and your releases
￼are something like "releases/20160519102200" (with "current" pointing
￼to last release), you need to adapt your configration.
￼
``` apacheconf
￼# Old configuration (without mod_realdoc):
￼<Directory "/var/www/your_host/current">
￼    Require all granted
￼</Directory>
￼
￼# New configuration (with mod_realdoc):
￼<Directory ~ "/var/www/your_host/releases/\d{14}">
￼    Require all granted
￼</Directory>
```
You need to adapt the regular expression `\d{14}` if you use a
different schema from timestamp in your releases.

