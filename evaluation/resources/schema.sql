CREATE TABLE employees (employee_id int, dept varchar(20), salary float);
CREATE TABLE sales (sale_id int, sold_date date, price float, sold_date_sk int);
CREATE TABLE date_dim (d_date_sk int primary key, d_date date, d_year int, d_moy int, d_dom int);
