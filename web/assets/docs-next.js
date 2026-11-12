/* PodStack Docs — interações do shell F0/F1.
   Drawer mobile, accordion da sidebar e sincronização TOC/sidebar. */
(() => {
  "use strict";

  const body = document.body;
  const sidebar = document.getElementById("dx-sidebar");
  const navToggle = document.querySelector("[data-dx-nav-toggle]");
  const overlay = document.querySelector("[data-dx-overlay]");

  /* ---------- Drawer mobile ---------- */

  function setNav(open) {
    body.classList.toggle("dx-nav-open", open);
    if (navToggle) {
      navToggle.setAttribute("aria-expanded", String(open));
      navToggle.setAttribute("aria-label", open ? "Fechar navegação" : "Abrir navegação");
    }
    if (open && sidebar) {
      const firstLink = sidebar.querySelector("a");
      if (firstLink) {
        window.setTimeout(() => firstLink.focus(), 120);
      }
    }
  }

  if (navToggle) {
    navToggle.addEventListener("click", () => {
      setNav(!body.classList.contains("dx-nav-open"));
    });
  }

  if (overlay) {
    overlay.addEventListener("click", () => setNav(false));
  }

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && body.classList.contains("dx-nav-open")) {
      setNav(false);
      if (navToggle) {
        navToggle.focus();
      }
    }
  });

  if (sidebar) {
    sidebar.querySelectorAll("a").forEach((link) => {
      link.addEventListener("click", () => {
        if (window.matchMedia("(max-width: 1023px)").matches) {
          setNav(false);
        }
      });
    });
  }

  /* ---------- Accordion ---------- */

  document.querySelectorAll("[data-dx-chevron]").forEach((button) => {
    button.addEventListener("click", () => {
      const expanded = button.getAttribute("aria-expanded") === "true";
      const target = document.getElementById(button.getAttribute("aria-controls") || "");
      button.setAttribute("aria-expanded", String(!expanded));
      if (target) {
        target.hidden = expanded;
      }
      const title =
        button.closest(".dx-nav-page")?.querySelector(".dx-nav-link span")?.textContent?.trim() ||
        "seção";
      button.setAttribute("aria-label", `${expanded ? "Expandir" : "Recolher"} seção ${title}`);
    });
  });

  /* ---------- TOC + subitem ativo ---------- */

  const headings = Array.from(
    document.querySelectorAll(".dx-page h2[id], .dx-page h3[id]")
  );
  const tocLinks = new Map(
    Array.from(document.querySelectorAll(".dx-toc a")).map((link) => [
      (link.getAttribute("href") || "").replace(/^#/, ""),
      link,
    ])
  );
  const activePage = document.querySelector(".dx-nav-page.is-active");
  const sideLinks = new Map(
    Array.from(activePage?.querySelectorAll(".dx-nav-sub a") || []).map((link) => [
      (link.hash || "").replace(/^#/, ""),
      link,
    ])
  );

  let ticking = false;

  function sync() {
    let active = headings[0]?.id || "";
    const boundary = 120;

    headings.forEach((heading) => {
      if (heading.getBoundingClientRect().top <= boundary) {
        active = heading.id;
      }
    });

    tocLinks.forEach((link, id) => {
      const on = id === active;
      link.classList.toggle("is-active", on);
      if (on) {
        link.setAttribute("aria-current", "location");
      } else {
        link.removeAttribute("aria-current");
      }
    });

    sideLinks.forEach((link, id) => {
      const on = id === active;
      link.classList.toggle("is-active", on);
      if (on) {
        link.setAttribute("aria-current", "location");
      } else {
        link.removeAttribute("aria-current");
      }
    });

    ticking = false;
  }

  function requestSync() {
    if (!ticking) {
      ticking = true;
      window.requestAnimationFrame(sync);
    }
  }

  if (headings.length > 0 && (tocLinks.size > 0 || sideLinks.size > 0)) {
    window.addEventListener("scroll", requestSync, { passive: true });
    window.addEventListener("resize", requestSync);
    sync();
  }

  /* ---------- Mantém a página ativa visível na sidebar ---------- */

  if (sidebar && activePage) {
    window.requestAnimationFrame(() => {
      const rect = activePage.getBoundingClientRect();
      const sidebarRect = sidebar.getBoundingClientRect();
      if (rect.top < sidebarRect.top || rect.bottom > sidebarRect.bottom) {
        sidebar.scrollTop += rect.top - sidebarRect.top - 24;
      }
    });
  }
})();
