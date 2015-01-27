# JSONB_EXTEND

Provides a function for PostgreSQL 9.4 to merge two or more `jsonb` values into one value. With one value for one key, last value wins.


## Installation

### Building a debian package (example for Ubuntu Linux 14.04)

 1. [Add official PostgreSQL repository](https://wiki.postgresql.org/wiki/Apt) to your system if necessary.

 2. Install required dependencies for build:

    ```sh
    sudo apt-get install git debhelper postgresql-server-dev-9.4 devscripts
    ```

 3. Clone source and go to it:

    ```sh
    git clone https://github.com/koctep/jsonb-extend.git
    cd jsonb-extend
    ```

 4. Build a package:

    ```sh
    debuild -i -us -uc -b
    ```

 5. Generated .deb will be placed in parent directory. You may install it with command like:

    ```sh
    dpkg -i ../postgresql-9.4-jsonb-extend_1.0-1_amd64.deb
    ```


## Usage

Enable `jsonb_extend` extension for your database:

```sql
CREATE EXTENSION jsonb_extend;
```

Use it as follows:

```sql
SELECT jsonb_extend('{"a": 5, "b": 6}'::jsonb, '{"b": 7, "c": 9}'::jsonb) AS new_jsonb;
```

That will return:

```
       new_jsonb
--------------------------
 {"a": 5, "b": 7, "c": 9}
```
