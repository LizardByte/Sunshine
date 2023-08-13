# Contributing to Sunshine Docs

Please read the [organizational level contribution](https://docs.lizardbyte.dev/en/latest/developers/contributing.html) docs first.

## Local Dev Environment

### Prerequisites
Install the following (latest versions should be fine):
* python
* doxygen

### Setup
1. cd into `docs`
1. Create a python virtual environment with: `python -m venv <name_of_env>`. This will store the python dependencies for generating the sphinx documentation.
2. Activate the environment with `./<name_of_env>/bin/activate`
3. Install python dependencies with `pip install -r requirements.txt`

### Generating Documentation
Execute `make html` to generate html documentation into `docs/builds`

