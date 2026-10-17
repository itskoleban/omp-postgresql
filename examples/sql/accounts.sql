-- Each declaration starts a single independent statement. Names are case-sensitive.
-- The sections.pwn regression creates disposable, session-local accounts fixtures.
-- A real gamemode manages its persistent schema separately.

-- name: load_account
SELECT id, username, money, score
FROM accounts
WHERE username = $1;

-- name: save_account
UPDATE accounts
SET money = $1,
    score = $2
WHERE id = $3;

-- name: delete_account
DELETE FROM accounts
WHERE id = $1;

-- name: save_all_accounts
UPDATE accounts AS a
SET money = v.money,
    score = v.score
FROM unnest(
    $1::integer[],
    $2::integer[],
    $3::integer[]
) AS v(id, money, score)
WHERE a.id = v.id;
