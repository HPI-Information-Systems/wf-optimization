SELECT *, rank() OVER (partition by dept order by salary, salary) res FROM employees;
