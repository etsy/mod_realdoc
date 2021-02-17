
default:
	apxs -c mod_realdoc.c

install:
	apxs -i -a -n realdoc mod_realdoc.la

