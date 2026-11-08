// The group switcher shared by the plan, story and outline pages: one button per listed group (the server lists only
// groups with something written, plus the reader's own), the shown one pressed; one click switches.
// `el` is a <fieldset> so disabling it disables every button. `current` is drawn even when unlisted (a developer
// opening an empty group by name). `onPick(group)` runs on a click on another group.
// The page's address follows the shown group (?group=), so a link opens the same group.
function groupButtons(el, groups, current, onPick) {
  const list = current != null && !groups.includes(current) ? [...groups, current] : groups;
  const url = new URL(location.href);
  if (current == null) url.searchParams.delete('group'); else url.searchParams.set('group', current);
  if (url.href !== location.href) history.replaceState(null, '', url);
  el.replaceChildren(...list.map((g) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chip';
    b.textContent = g || '（未分组）';
    b.setAttribute('aria-pressed', String(g === current));
    b.addEventListener('click', () => { if (g !== current) onPick(g); });
    return b;
  }));
  return list.length;
}
