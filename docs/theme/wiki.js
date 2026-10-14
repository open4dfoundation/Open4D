"use strict";
(() => {
  const mobile = window.matchMedia("(max-width: 760px)");
  const compact = window.matchMedia("(max-width: 1000px)");
  const navigation = document.querySelector("details.navigation");
  const contents = document.querySelector("details.page-contents");
  function layoutRails() {
    if (navigation) navigation.open = !mobile.matches;
    if (contents) contents.open = !compact.matches;
  }
  layoutRails();
  mobile.addEventListener("change", layoutRails);
  compact.addEventListener("change", layoutRails);

  const input = document.getElementById("wiki-search");
  const results = document.getElementById("search-results");
  const script = document.querySelector("script[data-search-url]");
  if (input && results && script) {
    const searchUrl = new URL(script.dataset.searchUrl, location.href);
    const base = new URL("../", searchUrl);
    let index;
    let loading;
    let request = 0;
    function showResults(show) {
      results.hidden = !show;
      input.setAttribute("aria-expanded", String(show));
    }
    function message(text) {
      const paragraph = document.createElement("p");
      paragraph.className = "search-message";
      paragraph.textContent = text;
      results.replaceChildren(paragraph);
    }
    async function search() {
      const current = ++request;
      const query = input.value.trim().toLowerCase();
      showResults(Boolean(query));
      if (!query) return;
      message("Searching documentation…");
      try {
        if (!index) {
          loading ||= fetch(searchUrl).then(response => {
            if (!response.ok) throw new Error("Search index unavailable");
            return response.json();
          }).then(data => data.docs);
          index = await loading;
        }
        if (request !== current) return;
        const terms = query.split(/\s+/);
        const hits = index.filter(doc => terms.every(term => (doc.title + " " + doc.text).toLowerCase().includes(term)))
          .sort((a, b) => Number(b.title.toLowerCase().includes(query)) - Number(a.title.toLowerCase().includes(query)))
          .slice(0, 8);
        results.replaceChildren();
        if (!hits.length) message("No matching pages. Try a method, codec or API name.");
        for (const hit of hits) {
          const url = new URL(hit.location, base);
          if (url.origin !== base.origin || !url.pathname.startsWith(base.pathname)) continue;
          const link = document.createElement("a");
          link.className = "search-hit";
          link.href = url.href;
          const title = document.createElement("strong");
          title.textContent = hit.title;
          const excerpt = document.createElement("span");
          const text = hit.text.replace(/\s+/g, " ").trim();
          const match = text.toLowerCase().indexOf(terms[0]);
          const start = Math.max(0, match - 35);
          excerpt.textContent = (start ? "…" : "") + text.slice(start, start + 130) + (text.length > start + 130 ? "…" : "");
          link.append(title, excerpt);
          results.append(link);
        }
      } catch {
        if (request === current) {
          index = undefined;
          loading = undefined;
          message("Search could not load. Browse the navigation, or try again.");
        }
      }
    }
    document.querySelector("form.search").addEventListener("submit", event => {
      event.preventDefault();
      results.querySelector("a")?.focus();
    });
    input.addEventListener("input", search);
    input.addEventListener("focus", () => { if (input.value.trim()) showResults(true); });
    input.addEventListener("keydown", event => {
      if (event.key === "Escape") { ++request; showResults(false); }
      if (event.key === "ArrowDown") { event.preventDefault(); results.querySelector("a")?.focus(); }
    });
    results.addEventListener("keydown", event => {
      const links = [...results.querySelectorAll("a")];
      const current = links.indexOf(document.activeElement);
      if (event.key === "ArrowDown") { event.preventDefault(); links[Math.min(current + 1, links.length - 1)]?.focus(); }
      if (event.key === "ArrowUp") { event.preventDefault(); if (current <= 0) input.focus(); else links[current - 1]?.focus(); }
      if (event.key === "Escape") { showResults(false); input.focus(); }
    });
    document.addEventListener("click", event => {
      if (!event.target.closest("form.search")) showResults(false);
    });
    document.addEventListener("keydown", event => {
      const editing = event.target.matches("input, textarea, select") || event.target.isContentEditable;
      if (!editing && (event.key === "/" || ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "k"))) {
        event.preventDefault();
        input.focus();
      }
    });
  }

  if (navigator.clipboard && window.isSecureContext) {
    document.querySelectorAll("article pre").forEach(pre => {
      const code = pre.querySelector("code");
      if (!code) return;
      const wrapper = document.createElement("div");
      wrapper.className = "code-block";
      pre.replaceWith(wrapper);
      wrapper.append(pre);
      const button = document.createElement("button");
      button.type = "button";
      button.className = "copy-code";
      button.textContent = "Copy";
      button.setAttribute("aria-label", "Copy code to clipboard");
      button.addEventListener("click", async () => {
        button.disabled = true;
        try {
          await navigator.clipboard.writeText(code.textContent);
          button.textContent = "Copied";
          button.dataset.state = "success";
        } catch {
          button.textContent = "Select code";
          button.dataset.state = "error";
          const range = document.createRange();
          range.selectNodeContents(code);
          const selection = window.getSelection();
          selection.removeAllRanges();
          selection.addRange(range);
        } finally {
          button.disabled = false;
          setTimeout(() => { button.textContent = "Copy"; delete button.dataset.state; }, 2500);
        }
      });
      wrapper.append(button);
    });
  }

  if ("IntersectionObserver" in window) {
    const links = [...document.querySelectorAll(".toc-list a")];
    const observed = new Map();
    for (const link of links) {
      const target = document.getElementById(decodeURIComponent(link.hash.slice(1)));
      if (target) observed.set(target, link);
    }
    const observer = new IntersectionObserver(entries => {
      const entry = entries.filter(item => item.isIntersecting).sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top)[0];
      if (!entry) return;
      links.forEach(link => link.removeAttribute("aria-current"));
      observed.get(entry.target).setAttribute("aria-current", "location");
    }, { rootMargin: "-140px 0px -65% 0px" });
    observed.forEach((link, target) => observer.observe(target));
  }
})();
