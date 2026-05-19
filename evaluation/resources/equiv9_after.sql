SELECT *,
       AVG(price) OVER (PARTITION BY sold_date) AS res
  FROM sales
       JOIN date_dim on sold_date_sk = d_date_sk
 WHERE d_year >= 2020
