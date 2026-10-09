// The login page's only script (the site's CSP allows no inline script).
'use strict';
if (location.search.includes('bad=1')) document.getElementById('bad').classList.remove('hide');
