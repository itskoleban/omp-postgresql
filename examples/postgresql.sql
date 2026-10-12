-- Run as an administrator, outside the gamemode. Set a password through a secure
-- administration workflow (e.g. psql \password), never commit a real password.
CREATE ROLE game_runtime LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION;
CREATE DATABASE game OWNER game_runtime;
-- Reconnect to game before creating application tables.
