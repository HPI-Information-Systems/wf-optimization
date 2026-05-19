SELECT *, 
       AVG(price) OVER (PARTITION BY sold_date, sold_date, sold_date, sold_date, sold_date, sold_date, sold_date, sold_date) AS res
  FROM sales;
