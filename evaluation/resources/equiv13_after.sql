-- does not work on Umbra without extra order by
  SELECT *
    FROM (SELECT *,
  	             rank() OVER (order by price, sale_id) res FROM sales) AS s
-- ORDER BY price
   LIMIT 3;
