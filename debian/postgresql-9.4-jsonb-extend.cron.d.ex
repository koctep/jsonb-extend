#
# Regular cron jobs for the postgresql-9.4-jsonb-extend package
#
0 4	* * *	root	[ -x /usr/bin/postgresql-9.4-jsonb-extend_maintenance ] && /usr/bin/postgresql-9.4-jsonb-extend_maintenance
