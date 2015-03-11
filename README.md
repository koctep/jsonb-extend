# JSONB_EXTEND

Provides functions for PostgreSQL 9.4 to merge two or more `jsonb` values into one value.  
Provided functions:
* `jsonb_extend(a jsonb, b jsonb)`  
`a` - jsonb object/array  
`b` - jsonb object/array/value  
If `a` is jsonb object then `b` should be jsonb object to.

* `jsonb_deep_extend(a jsonb, b jsonb)`  
`a` - jsonb object  
`b` - jsonb object

## Usage examples

### Concat arrays:
```sql
select jsonb_extend('[1]'::jsonb, '[2]'::jsonb);
 jsonb_extend 
--------------
 [1, 2]
```
### Append a value to array:
```sql
select jsonb_extend('[1]'::jsonb, '2'::jsonb);
 jsonb_extend 
--------------
 [1, 2]
 ```
 ```sql
 select jsonb_extend('[1]'::jsonb, '{"a": 2}'::jsonb);
 jsonb_extend  
---------------
 [1, {"a": 2}]
```
### Merging two objects
```sql
SELECT jsonb_extend('{"a": 5, "b": 6}'::jsonb, '{"b": 7, "c": 9}'::jsonb) AS new_jsonb;
        new_jsonb
--------------------------
 {"a": 5, "b": 7, "c": 9}
 ```
 ```sql
SELECT jsonb_extend('{"a": {"b": 6}}'::jsonb, '{"a": {"c": 7}}'::jsonb) AS new_jsonb;
            new_jsonb
---------------------------------
 {"a": {"c": 7}}
```
```sql
SELECT jsonb_deep_extend('{"a": {"b": 6}}'::jsonb, '{"a": {"c": 7}}'::jsonb) AS new_jsonb;
        new_jsonb
-------------------------
 {"a": {"b": 6, "c": 7}}
```

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

 6. Enable `jsonb_extend` extension for your database:  
```sql
CREATE EXTENSION jsonb_extend;
```
