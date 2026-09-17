"""Local, reproducible qsbit-sim documentation build."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent / '_ext'))
project = 'qsbit-sim'
author = 'qsbit-sim contributors'
release = '0.1.0'
extensions = ['myst_parser', 'sphinx.ext.graphviz', 'breathe', 'qsbit_docs']
source_suffix = {'.md': 'markdown'}
exclude_patterns = ['_build', '_ext', '_templates', 'reviews']
myst_heading_anchors = 6
myst_enable_extensions = ['colon_fence']
html_theme = 'furo'
html_title = 'qsbit-sim'
html_static_path = ['_static']
templates_path = ['_templates']
html_css_files = ['site.css']
html_js_files = ['trace-player.js', 'architecture.js']
html_theme_options = {
    'light_css_variables': {'color-brand-primary': '#126d77', 'color-brand-content': '#126d77'},
    'dark_css_variables': {'color-brand-primary': '#70c8ce', 'color-brand-content': '#70c8ce'},
}
graphviz_output_format = 'svg'
breathe_default_project = 'qsbit'
breathe_domain_by_extension = {'hpp': 'cpp'}
# Standard-library declarations live outside the extracted project API.
nitpick_ignore_regex = [('cpp:identifier', r'std::.*'), ('cpp:identifier', r'sc_core::.*')]

# External namespace and opaque implementation forward declarations.
nitpick_ignore = [('cpp:identifier', 'sc_core'), ('cpp:identifier', 'Impl')]

html_show_copyright = False

html_baseurl = 'https://qsbit-org.github.io/qsbit-sim/'
