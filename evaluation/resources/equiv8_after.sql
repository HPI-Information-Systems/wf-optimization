SELECT *
  FROM (SELECT *,
  	           AVG(price) OVER (PARTITION BY EXTRACT(YEAR FROM sold_date)) AS res
          FROM sales
         WHERE EXTRACT(YEAR FROM sold_date) >= 2020) AS s
 WHERE sold_date >= DATE'2020-02-15'
