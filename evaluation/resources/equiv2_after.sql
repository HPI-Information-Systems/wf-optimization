SELECT *, rank() OVER (partition by dept order by salary) res FROM employees;
