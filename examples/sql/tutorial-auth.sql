-- Apply manually with psql to a disposable database, using its schema-owner role.
-- Never submit this file as an automatic application migration.
CREATE TABLE tutorial_accounts (
    id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username text NOT NULL UNIQUE CHECK (length(username) BETWEEN 1 AND 24),
    password_hash text NOT NULL CHECK (length(password_hash) = 60),
    score integer NOT NULL DEFAULT 0 CHECK (score >= 0)
);
