SELECT * FROM sales, (SELECT avg(price) res FROM sales);
