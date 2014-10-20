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

