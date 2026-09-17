# Build and preview the documentation website

Run commands from the repository root. The website requires Doxygen, Graphviz and
the optional `docs` Python extra. On Ubuntu, install the system tools with
`sudo apt install doxygen graphviz`. Simulator and example prerequisites are listed
in [prerequisites](prerequisites.md).

```sh
uv sync --frozen --extra docs
source .venv/bin/activate
cmake --preset gcc-ninja -DBUILD_TESTING=ON
cmake --build --preset gcc-ninja --parallel
python -m sphinx -n -W --keep-going -b html -D qsbit_build=build-gcc docs out/documentation-preview/html
python -m http.server 8000 --bind 127.0.0.1 --directory out/documentation-preview/html
```

Open `http://localhost:8000`. The server must remain running while browsing. For pip,
create and activate `.venv`, then install `python -m pip install -e '.[docs]'`.
Package constraints and locked resolutions are in [pyproject.toml](../pyproject.toml)
and [uv.lock](../uv.lock). No numerical quantum backend is needed for the website.

The Sphinx build extracts C++ declarations and runs the compiled Bell/feedback ELF
examples with scripted outcomes. It checks their schedules before writing playback
data. Select another configured build using `-D qsbit_build=BUILD_DIRECTORY`.
HTML, Doxygen XML and execution traces remain under the output build directory;
building documentation does not rewrite tracked Markdown or headers.

## Authoring

Edit existing Markdown under `docs/`. Add pages to the appropriate toctree. Each
module page includes concrete objects/state, behavior, a Graphviz diagram and CTest
evidence. `api.md` uses Doxygen/Breathe to read the current header declarations.
Source-file links on the website target the checkout's Git revision; local Markdown
links still open the source files directly.

The architecture graph contains one linked box per logical module. Graphviz SVG
links resolve from `_images` to module pages and open in the top-level page. Keep
both the graph and the keyboard-accessible module index complete when adding a module.

The player reads the build-generated trace bundle. New displays must use recorded
fields or explicitly labeled derived clock values. A last observed value must show
its observation tick. Private queue contents and pipeline latches require a future
observation interface; retirement events cannot reconstruct them.

## Verification

```sh
ctest --test-dir build-gcc -L fast --output-on-failure
uv sync --frozen --extra docs --extra docs-test
python -m playwright install chromium
cmake --preset gcc-ninja -DBUILD_TESTING=ON -DQSBIT_TEST_WEBSITE=ON
ctest --test-dir build-gcc -L website --output-on-failure
```

The browser test serves the site locally, checks module links and rendered diagrams,
steps through executions and validates observable values. CI additionally builds
with Sphinx warnings treated as errors and uploads the HTML as a private workflow
artifact. There is no deployment step.
