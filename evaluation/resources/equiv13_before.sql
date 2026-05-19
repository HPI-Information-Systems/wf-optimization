SELECT *
  FROM (SELECT *,
  	           rank() OVER (order by price, sale_id) res
  	      FROM sales) AS s
 WHERE res <= 20;
