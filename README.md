mod_realdoc is an Apache module which does a realpath on
the docroot symlink and sets the absolute path as the
real document root for the remainder of the request.

It executes after mod_rewrite and before a request handler
like PHP.

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

The one caveat which we have explicitly chosen to not
attempt to resolve is that there is still a possible race
condition between .htaccess mod_rewrite rules and the code.
For reasons too complex to go into mod_rewrite rules have
to run with the Apache configured docroot in place so we
do the docroot change after these rules have run. This means
that if a mod_rewrite redirect rule is changed from an
existing target to a new target, this rule will come into
effect before the target file is available. 
