SELECT *, rank() OVER (partition by dept order by salary, salary, salary, salary, salary, salary) res FROM employees;
