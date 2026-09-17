/* Zoom the SVG without changing its node links or layout. */
(() => {
  if (!document.getElementById('diagram-fit')) return;
  const diagram = document.querySelector('object.graphviz');
  const viewport = diagram.parentElement;
  viewport.classList.add('architecture-viewport');
  let naturalWidth = 1000, ratio = 1, width = 1000;
  const resize = value => {
    width = Math.max(320, Math.min(value, naturalWidth * 3));
    diagram.style.width = `${width}px`;
    diagram.style.height = `${width * ratio}px`;
  };
  function ready() {
    const svg = diagram.contentDocument?.querySelector('svg');
    if (!svg) return;
    const box = svg.viewBox.baseVal;
    naturalWidth = box.width * 1.15;
    ratio = box.height / box.width;
    resize(naturalWidth);
  }
  diagram.addEventListener('load', ready);
  ready();
  document.getElementById('diagram-in').onclick = () => resize(width * 1.25);
  document.getElementById('diagram-out').onclick = () => resize(width / 1.25);
  document.getElementById('diagram-fit').onclick = () => resize(viewport.clientWidth - 12);
  document.getElementById('diagram-actual').onclick = () => resize(naturalWidth);
  document.getElementById('diagram-open').href = diagram.data;
})();
