/* PodStack Docs — interações do shell F0/F1.
   Drawer mobile, accordion da sidebar e sincronização TOC/sidebar. */
(() => {
  "use strict";

  const body = document.body;
  const sidebar = document.getElementById("dx-sidebar");
  const navToggle = document.querySelector("[data-dx-nav-toggle]");
  const overlay = document.querySelector("[data-dx-overlay]");
  const main = document.getElementById("conteudo");
  const mobileNav = window.matchMedia("(max-width: 1023px)");

  /* ---------- Drawer mobile ---------- */

  function setNav(open, restoreFocus) {
    body.classList.toggle("dx-nav-open", open);
    if (navToggle) {
      navToggle.setAttribute("aria-expanded", String(open));
      navToggle.setAttribute("aria-label", open ? "Fechar navegação" : "Abrir navegação");
    }
    // Inert no conteúdo atrás do drawer: o foco e os leitores de tela não
    // escapam para a página enquanto o menu está aberto.
    if (main) {
      main.inert = open && mobileNav.matches;
    }
    if (open && navToggle) {
      // O toggle é o primeiro elemento do ciclo de foco (Tab entra na sidebar).
      window.setTimeout(() => navToggle.focus(), 120);
    } else if (restoreFocus && navToggle) {
      navToggle.focus();
    }
  }

  if (navToggle) {
    navToggle.addEventListener("click", () => {
      setNav(!body.classList.contains("dx-nav-open"));
    });
  }

  if (overlay) {
    overlay.addEventListener("click", () => setNav(false, true));
  }

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && body.classList.contains("dx-nav-open")) {
      setNav(false, true);
      return;
    }
    if (event.key !== "Tab" || !body.classList.contains("dx-nav-open") || !mobileNav.matches) {
      return;
    }
    // Ciclo de foco do drawer: toggle + links/botões da sidebar.
    const focusables = [navToggle, ...(sidebar ? sidebar.querySelectorAll("a, button") : [])].filter(
      (element) => element && element.offsetParent !== null,
    );
    if (focusables.length === 0) {
      return;
    }
    const first = focusables[0];
    const last = focusables[focusables.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  });

  // Se a janela crescer para desktop com o drawer aberto, fecha e limpa o
  // `inert` para a página voltar a ser navegável.
  const onMobileNavChange = (event) => {
    if (!event.matches && body.classList.contains("dx-nav-open")) {
      setNav(false);
    }
  };
  if (typeof mobileNav.addEventListener === "function") {
    mobileNav.addEventListener("change", onMobileNavChange);
  } else if (typeof mobileNav.addListener === "function") {
    mobileNav.addListener(onMobileNavChange);
  }

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

  /* ---------- Permalinks de headings ---------- */

  document.querySelectorAll(".dx-page h2[id], .dx-page h3[id]").forEach((heading) => {
    const label = heading.textContent.replace(/\s+/g, " ").trim();
    const anchor = document.createElement("a");
    anchor.className = "heading-anchor";
    anchor.href = `#${heading.id}`;
    anchor.setAttribute("aria-label", `Link para ${label}`);
    anchor.textContent = "#";
    heading.append(anchor);
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
