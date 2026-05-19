SELECT *, 
       AVG(price) OVER (PARTITION BY sold_date) AS res
  FROM sales
