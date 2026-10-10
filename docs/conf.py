"""Local, reproducible qsbit-sim documentation build."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "_ext"))
project = "qsbit-sim"
author = "qsbit-sim contributors"
release = "0.1.0"
extensions = ["myst_parser", "sphinx.ext.graphviz", "breathe", "sphinx_copybutton", "qsbit_docs"]
source_suffix = {".md": "markdown"}
exclude_patterns = ["_build", "_ext", "_templates", "reviews"]
myst_heading_anchors = 6
myst_enable_extensions = ["colon_fence"]
html_theme = "pydata_sphinx_theme"
html_title = "qsbit-sim"
html_static_path = ["../web", "_static"]
templates_path = ["_templates"]
html_css_files = [
    "qsbit-fonts.css",
    "qsbit-tokens.css",
    "qsbit-base.css",
    "trace-player.css",
    "docs-theme.css",
]
html_js_files = [
    (name, {"defer": "defer"}) for name in ["trace-model.js", "trace-player.js", "architecture.js"]
]
html_theme_options = {
    "navbar_start": ["navbar-logo"],
    "navbar_center": [],
    "navbar_end": ["theme-switcher", "navbar-icon-links"],
    "github_url": "https://github.com/qsbit-org/qsbit-sim",
    "show_nav_level": 2,
    "navigation_depth": 3,
    "show_prev_next": False,
    "secondary_sidebar_items": ["page-toc"],
}
html_sidebars = {"**": ["qsbit-navigation.html"], "execution": []}
graphviz_output_format = "svg"
breathe_default_project = "qsbit"
breathe_domain_by_extension = {"hpp": "cpp"}
# Standard-library declarations live outside the extracted project API.
nitpick_ignore_regex = [("cpp:identifier", r"std::.*"), ("cpp:identifier", r"sc_core::.*")]

# External namespace and opaque implementation forward declarations.
nitpick_ignore = [("cpp:identifier", "sc_core"), ("cpp:identifier", "Impl")]

html_show_copyright = False

html_baseurl = "https://qsbit-org.github.io/qsbit-sim/"
