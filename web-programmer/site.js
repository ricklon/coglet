// The route through the project, in the order to work through it. Every page is plain HTML;
// this only adds the stage bar and the Back/Next links, so the pages still read without it.
const PAGES = [
  { href: 'index.html',     label: 'Overview',  title: 'Project overview' },
  { href: 'parts.html',     label: 'Parts',     title: 'Parts and tools' },
  { href: 'flash.html',     label: 'Flash',     title: 'Flash the firmware' },
  { href: 'wiring.html',    label: 'Wire',      title: 'Wire it, step by step' },
  { href: 'console.html',   label: 'Check',     title: 'Check the board' },
  { href: 'hub.html',       label: 'Connect',   title: 'Connect to agent-hub' },
  { href: 'build.html',     label: 'Build',     title: 'Build the mechanism' },
  { href: 'calibrate.html', label: 'Calibrate', title: 'Calibrate the servos' },
];
(function () {
  const here = (location.pathname.split('/').pop() || 'index.html');
  const i = PAGES.findIndex(p => p.href === here);
  if (i < 0) return;
  const nav = document.createElement('nav');
  nav.className = 'site-nav'; nav.setAttribute('aria-label', 'Project stages');
  PAGES.forEach((p, k) => {
    const a = document.createElement('a'); a.href = p.href;
    a.innerHTML = (k ? `<span class="n">${k}</span>` : '') + p.label;
    if (k === i) a.setAttribute('aria-current', 'page');
    nav.appendChild(a);
  });
  document.body.insertBefore(nav, document.body.firstChild);
  const pager = document.createElement('div'); pager.className = 'site-pager';
  const link = (p, cls, dir) => { const a = document.createElement('a'); a.href = p.href; a.className = cls;
    a.innerHTML = `<small>${dir}</small>${p.title}`; return a; };
  pager.appendChild(i > 0 ? link(PAGES[i - 1], 'prev', 'Back') : Object.assign(document.createElement('span'), { className: 'spacer' }));
  pager.appendChild(i < PAGES.length - 1 ? link(PAGES[i + 1], 'next', 'Next') : Object.assign(document.createElement('span'), { className: 'spacer' }));
  document.body.appendChild(pager);
})();
