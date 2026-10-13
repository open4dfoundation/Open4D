"use strict";
(() => {
  const input = document.getElementById("wiki-search");
  const results = document.getElementById("search-results");
  const script = document.querySelector("script[data-search-url]");
  if (!input || !results || !script) return;
  const searchUrl = new URL(script.dataset.searchUrl, location.href);
  const base = new URL("../", searchUrl);
  let index;
  let request = 0;
  document.querySelector("form.search").addEventListener("submit", event => event.preventDefault());
  input.addEventListener("input", async () => {
    const current = ++request;
    const query = input.value.trim().toLowerCase();
    results.replaceChildren();
    results.hidden = !query;
    if (!query) return;
    results.textContent = "Searching...";
    try {
      if (!index) {
        const response = await fetch(searchUrl);
        if (!response.ok) throw new Error("search index unavailable");
        index = (await response.json()).docs;
      }
      if (request !== current) return;
      const terms = query.split(/\s+/);
      const hits = index.filter(doc => terms.every(term => (doc.title + " " + doc.text).toLowerCase().includes(term))).slice(0, 8);
      results.replaceChildren();
      if (!hits.length) results.textContent = "No pages found. Try a codec or API name.";
      for (const hit of hits) {
        const link = document.createElement("a");
        const url = new URL(hit.location, base);
        if (url.origin !== base.origin || !url.pathname.startsWith(base.pathname)) continue;
        link.href = url.href;
        link.textContent = hit.title;
        results.append(link);
      }
    } catch {
      if (request === current) {
        index = undefined;
        results.textContent = "Search is unavailable. Use the documentation navigation.";
      }
    }
  });
  input.addEventListener("keydown", event => {
    if (event.key === "Escape") { ++request; results.hidden = true; }
    if (event.key === "ArrowDown") { event.preventDefault(); results.querySelector("a")?.focus(); }
  });
})();
