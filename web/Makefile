.PHONY: install run up start lint lint-file fix deploy

install:
	uv sync --all-groups

# uv parses the env file itself and hands the result to the child process only.
# Sourcing it into the shell instead would run it as shell code and leave the
# values behind, where compose would then prefer them over the file. Optional,
# so plain exported variables still work if there's no deploy/.env.
ENV_FILE := deploy/.env
ENV_ARG := $(if $(wildcard $(ENV_FILE)),--env-file $(ENV_FILE),)

run:
	uv run $(ENV_ARG) uvicorn app.main:app --host 0.0.0.0 --port 8001 --reload --reload-include "*.html"

up: run
start: run

deploy:
	@test -f deploy/.env || (echo "ERROR: deploy/.env not found — copy deploy/.env.sample and fill in values" && exit 1)
	docker compose -f deploy/compose.yml up --build -d

lint:
	uv run ruff check .
	uv run ruff format --check .

lint-file:
	uv run ruff check $(FILE)
	uv run ruff format --check $(FILE)

fix:
	uv run ruff check --fix .
	uv run ruff format .
