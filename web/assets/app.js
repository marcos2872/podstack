/* PodStack Docs — interações sem dependências.
   Tema persistido, busca local, menu mobile, copiar código e TOC. */
(() => {
  "use strict";

  const root = document.documentElement;
  const storageKey = "podstack-docs-theme";

  /* ---------- Tema ---------- */

  const themeButton = document.getElementById("theme-toggle");
  const media = window.matchMedia("(prefers-color-scheme: dark)");

  function resolvedTheme() {
    const explicit = root.dataset.theme;
    if (explicit === "light" || explicit === "dark") {
      return explicit;
    }
    return media.matches ? "dark" : "light";
  }

  function themeIcon(theme) {
    if (theme === "dark") {
      // Sol: ação para ir ao tema claro.
      return '<svg class="icon" aria-hidden="true"><use href="#i-sun"></use></svg>';
    }
    return '<svg class="icon" aria-hidden="true"><use href="#i-moon"></use></svg>';
  }

  function syncThemeButton() {
    if (!themeButton) {
      return;
    }
    const theme = resolvedTheme();
    themeButton.innerHTML = themeIcon(theme);
    const next = theme === "dark" ? "claro" : "escuro";
    themeButton.setAttribute("aria-label", `Usar tema ${next}`);
    themeButton.setAttribute("title", `Usar tema ${next}`);
  }

  if (themeButton) {
    syncThemeButton();
    themeButton.addEventListener("click", () => {
      const next = resolvedTheme() === "dark" ? "light" : "dark";
      root.dataset.theme = next;
      try {
        localStorage.setItem(storageKey, next);
      } catch (_) {
        /* localStorage indisponível (file:// restrito ou modo privado). */
      }
      syncThemeButton();
    });
    if (typeof media.addEventListener === "function") {
      media.addEventListener("change", syncThemeButton);
    } else if (typeof media.addListener === "function") {
      media.addListener(syncThemeButton);
    }
  }

  /* ---------- Menu mobile ---------- */

  const navToggle = document.querySelector("[data-nav-toggle]");
  const navOverlay = document.querySelector("[data-nav-overlay]");
  const sidebar = document.getElementById("sidebar");

  function setNav(open) {
    document.body.classList.toggle("nav-open", open);
    if (navToggle) {
      navToggle.setAttribute("aria-expanded", String(open));
      navToggle.setAttribute("aria-label", open ? "Fechar navegação" : "Abrir navegação");
    }
    if (open) {
      const firstLink = sidebar && sidebar.querySelector("a");
      if (firstLink) {
        window.setTimeout(() => firstLink.focus(), 120);
      }
    }
  }

  if (navToggle) {
    navToggle.addEventListener("click", () => {
      setNav(!document.body.classList.contains("nav-open"));
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && document.body.classList.contains("nav-open")) {
        setNav(false);
        navToggle.focus();
      }
    });
  }

  if (navOverlay) {
    navOverlay.addEventListener("click", () => setNav(false));
  }

  document.querySelectorAll(".sidebar a").forEach((link) => {
    link.addEventListener("click", () => {
      if (window.matchMedia("(max-width: 900px)").matches) {
        setNav(false);
      }
    });
  });

  /* ---------- Destaque da página atual ---------- */

  const currentFile = (() => {
    const part = window.location.pathname.split("/").pop();
    return part && part.length > 0 ? part : "index.html";
  })();

  document.querySelectorAll(".nav-section a").forEach((link) => {
    const href = (link.getAttribute("href") || "").split("#")[0];
    if (href === currentFile) {
      link.setAttribute("aria-current", "page");
    }
  });

  /* ---------- Busca ---------- */

  const pageIndex = [
    {
      title: "Visão geral",
      url: "index.html",
      description: "O que é o PodStack, arquitetura, primeiros passos e limitações.",
      keys: "podstack visao geral arquitetura json-rpc daemon libpod instalacao setup rootless limitacoes"
    },
    {
      title: "Containers",
      url: "containers.html",
      description: "Listar, criar, operar, inspecionar, logs, terminal, arquivos e stats.",
      keys: "container containers podman criar iniciar parar pausar matar remover logs terminal arquivos stats inspect health rootless rootful checkpoint criu"
    },
    {
      title: "Imagens",
      url: "images.html",
      description: "Pull, push, tag, histórico, prune e export/load de imagens.",
      keys: "imagem imagens pull push tag untag prune digest history export load tar plataforma amd64 arm64"
    },
    {
      title: "Volumes",
      url: "volumes.html",
      description: "Criar, clonar, importar do host, exportar, importar e limpar volumes.",
      keys: "volume volumes driver mountpoint clone reflink export import tar zst prune host"
    },
    {
      title: "Redes",
      url: "networks.html",
      description: "Redes bridge, subnet, gateway, conectar containers e aliases.",
      keys: "rede redes network subnet gateway bridge conectar desconectar alias ipv6"
    },
    {
      title: "Pods",
      url: "pods.html",
      description: "Pods do Podman: criar, iniciar, parar, reiniciar e remover.",
      keys: "pod pods podman agrupar containers start stop restart remover"
    },
    {
      title: "Compose",
      url: "compose.html",
      description: "Projetos Compose por labels, up/down, logs por serviço e provider.",
      keys: "compose docker-compose projeto labels servico up down logs provider yaml"
    },
    {
      title: "Builds",
      url: "builds.html",
      description: "Build por contexto, Dockerfile, build args, plataformas e histórico.",
      keys: "build builds dockerfile contexto tag build-arg plataforma manifest list sqlite historico"
    },
    {
      title: "Machines",
      url: "machines.html",
      description: "VMs Linux QEMU/KVM, SSH, arquivos, portas, snapshots e hardware.",
      keys: "machine machines vm qemu kvm ssh virtiofs cloud-init snapshot usb serial audio x11 clone export import"
    },
    {
      title: "Kubernetes",
      url: "kubernetes.html",
      description: "Cluster k3s em VM, kubeconfig isolado, Pods, Services e port-forward.",
      keys: "kubernetes k8s k3s kubectl kubeconfig contexto pods services port-forward tls"
    },
    {
      title: "Domínios",
      url: "domains.html",
      description: "*.localhost, *.podstack.local, HTTPS com CA interna e labels.",
      keys: "dominio dominios localhost podstack.local proxy http https caddy ca dns labels expose lan"
    },
    {
      title: "Configurações",
      url: "settings.html",
      description: "Categorias, chaves, tema, tray, proxy, portas, machines e updates.",
      keys: "config configuracoes settings tema escala tray proxy portas maquinas retencao diagnostico updates"
    },
    {
      title: "CLI",
      url: "cli.html",
      description: "Sintaxe, comandos, flags, --json, exit codes e variáveis de ambiente.",
      keys: "cli podstack comando comandos flags json exit codes socket daemon automacao"
    },
    {
      title: "Solução de problemas",
      url: "troubleshooting.html",
      description: "Daemon, Podman, polkit, KVM, SSH, portas, CA, DNS, CRIU e report.",
      keys: "troubleshooting erro problema daemon socket podman polkit helper kvm virtiofsd ssh x11 portas firewall ca dns audio criu doctor report logs"
    }
  ];

  const searchInput = document.getElementById("site-search");
  const searchResults = document.getElementById("search-results");
  const searchBox = document.querySelector(".search");

  function normalize(value) {
    return String(value || "")
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .toLowerCase();
  }

  function localHeadings(query) {
    const items = [];
    document.querySelectorAll(".page h2[id], .page h3[id]").forEach((heading) => {
      const text = heading.textContent.replace(/\s+/g, " ").trim();
      if (normalize(text).includes(query)) {
        items.push({
          title: text,
          url: `#${heading.id}`,
          description: "Nesta página",
          local: true
        });
      }
    });
    return items;
  }

  function closeSearch(clearInput) {
    if (!searchResults) {
      return;
    }
    searchResults.hidden = true;
    searchResults.innerHTML = "";
    filterSidebar("");
    if (searchInput) {
      searchInput.setAttribute("aria-expanded", "false");
      if (clearInput) {
        searchInput.value = "";
      }
    }
  }

  function filterSidebar(query, matchedFiles) {
    const matched = matchedFiles || new Set();
    document.querySelectorAll(".nav-section").forEach((section) => {
      let visible = 0;
      section.querySelectorAll("li").forEach((item) => {
        const link = item.querySelector("a");
        if (!link) {
          return;
        }
        const href = (link.getAttribute("href") || "").split("#")[0];
        const haystack = normalize(`${link.textContent} ${link.dataset.search || ""}`);
        const match =
          query.length === 0 || matched.has(href) || haystack.includes(query);
        item.hidden = !match;
        if (match) {
          visible += 1;
        }
      });
      section.hidden = query.length > 0 && visible === 0;
    });
  }

  function renderResults(query) {
    if (!searchResults) {
      return;
    }

    const results = [];
    if (query.length > 0) {
      pageIndex.forEach((page) => {
        const haystack = normalize(`${page.title} ${page.description} ${page.keys}`);
        if (haystack.includes(query)) {
          results.push(page);
        }
      });
      results.push(...localHeadings(query));
    }

    searchResults.innerHTML = "";

    if (query.length === 0) {
      closeSearch();
      return;
    }

    const matchedFiles = new Set();
    results.forEach((item) => {
      if (item.local) {
        matchedFiles.add(currentFile);
      } else {
        matchedFiles.add(item.url.split("#")[0]);
      }
    });
    filterSidebar(query, matchedFiles);

    if (results.length === 0) {
      const empty = document.createElement("p");
      empty.className = "search-empty";
      empty.textContent = `Nenhum resultado para “${searchInput.value.trim()}”.`;
      searchResults.appendChild(empty);
    } else {
      results.slice(0, 10).forEach((item, index) => {
        const link = document.createElement("a");
        link.href = item.url;
        link.setAttribute("role", "option");
        link.dataset.index = String(index);
        if (index === 0) {
          link.classList.add("is-active");
          link.setAttribute("aria-selected", "true");
        } else {
          link.setAttribute("aria-selected", "false");
        }

        const title = document.createElement("span");
        title.className = "sr-title";
        title.textContent = item.title;
        const meta = document.createElement("span");
        meta.className = "sr-meta";
        meta.textContent = item.description;
        link.append(title, meta);
        searchResults.appendChild(link);
      });
    }

    searchResults.hidden = false;
    searchInput.setAttribute("aria-expanded", "true");
  }

  function moveSearchSelection(direction) {
    if (!searchResults) {
      return;
    }
    const options = Array.from(searchResults.querySelectorAll("a"));
    if (options.length === 0) {
      return;
    }
    const current = options.findIndex((option) => option.classList.contains("is-active"));
    const next = (current + direction + options.length) % options.length;
    options.forEach((option, index) => {
      const active = index === next;
      option.classList.toggle("is-active", active);
      option.setAttribute("aria-selected", String(active));
      if (active) {
        option.scrollIntoView({ block: "nearest" });
      }
    });
  }

  if (searchInput) {
    searchInput.addEventListener("input", () => {
      renderResults(normalize(searchInput.value.trim()));
    });

    searchInput.addEventListener("focus", () => {
      if (searchInput.value.trim().length > 0) {
        renderResults(normalize(searchInput.value.trim()));
      }
    });

    searchInput.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown") {
        event.preventDefault();
        moveSearchSelection(1);
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        moveSearchSelection(-1);
      } else if (event.key === "Enter") {
        const active = searchResults && searchResults.querySelector("a.is-active");
        if (active) {
          event.preventDefault();
          closeSearch(false);
          window.location.href = active.getAttribute("href");
        }
      } else if (event.key === "Escape") {
        closeSearch(true);
        searchInput.blur();
      }
    });

    if (searchResults) {
      searchResults.addEventListener("click", (event) => {
        if (event.target.closest("a")) {
          closeSearch(false);
        }
      });
    }

    document.addEventListener("click", (event) => {
      if (searchBox && !searchBox.contains(event.target)) {
        closeSearch();
      }
    });

    document.addEventListener("keydown", (event) => {
      const target = event.target;
      const typing =
        target instanceof HTMLElement &&
        (target.tagName === "INPUT" ||
          target.tagName === "TEXTAREA" ||
          target.tagName === "SELECT" ||
          target.isContentEditable);
      if (event.key === "/" && !typing && !event.metaKey && !event.ctrlKey && !event.altKey) {
        event.preventDefault();
        searchInput.focus();
      }
    });
  }

  /* ---------- Copiar código ---------- */

  const copyIcon =
    '<svg class="icon" aria-hidden="true"><use href="#i-copy"></use></svg>';
  const checkIcon =
    '<svg class="icon" aria-hidden="true"><use href="#i-check"></use></svg>';

  function fallbackCopy(text) {
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.top = "-1000px";
    area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch (_) {
      ok = false;
    }
    area.remove();
    return ok;
  }

  document.querySelectorAll("pre").forEach((pre) => {
    if (pre.closest(".code-block")) {
      return;
    }

    const wrapper = document.createElement("div");
    wrapper.className = "code-block";
    pre.parentNode.insertBefore(wrapper, pre);
    wrapper.appendChild(pre);

    const head = document.createElement("div");
    head.className = "code-head";

    const label = document.createElement("span");
    label.textContent = pre.dataset.lang || "shell";

    const button = document.createElement("button");
    button.type = "button";
    button.className = "copy-btn";
    button.setAttribute("aria-label", "Copiar bloco de código");
    button.innerHTML = `${copyIcon}<span>Copiar</span>`;

    head.append(label, button);
    wrapper.prepend(head);

    button.addEventListener("click", async () => {
      const text = pre.innerText.replace(/\n$/, "");
      let copied = false;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        try {
          await navigator.clipboard.writeText(text);
          copied = true;
        } catch (_) {
          copied = fallbackCopy(text);
        }
      } else {
        copied = fallbackCopy(text);
      }

      button.innerHTML = copied
        ? `${checkIcon}<span>Copiado!</span>`
        : `${copyIcon}<span>Copie manualmente</span>`;
      button.classList.toggle("copied", copied);
      window.setTimeout(() => {
        button.innerHTML = `${copyIcon}<span>Copiar</span>`;
        button.classList.remove("copied");
      }, 1800);
    });
  });

  /* ---------- Nesta página ---------- */

  const toc = document.querySelector("[data-toc]");
  if (toc) {
    const headings = Array.from(document.querySelectorAll(".page > h2[id]"));
    if (headings.length >= 2) {
      const meta = document.querySelector(".page-meta");
      if (meta) {
        meta.insertAdjacentElement("afterend", toc);
      }

      const title = document.createElement("p");
      title.className = "toc-title";
      title.textContent = "Nesta página";

      const list = document.createElement("ul");
      headings.forEach((heading) => {
        const item = document.createElement("li");
        const link = document.createElement("a");
        link.href = `#${heading.id}`;
        link.textContent = heading.textContent.replace(/\s+/g, " ").trim();
        item.appendChild(link);
        list.appendChild(item);
      });

      toc.append(title, list);
      toc.hidden = false;

      const links = Array.from(list.querySelectorAll("a"));
      let ticking = false;

      function syncToc() {
        let active = headings[0] ? headings[0].id : "";
        const boundary = 140;
        headings.forEach((heading) => {
          if (heading.getBoundingClientRect().top <= boundary) {
            active = heading.id;
          }
        });
        links.forEach((link) => {
          if (link.getAttribute("href") === `#${active}`) {
            link.setAttribute("aria-current", "true");
          } else {
            link.removeAttribute("aria-current");
          }
        });
        ticking = false;
      }

      function requestSync() {
        if (!ticking) {
          ticking = true;
          window.requestAnimationFrame(syncToc);
        }
      }

      window.addEventListener("scroll", requestSync, { passive: true });
      window.addEventListener("resize", requestSync);
      syncToc();
    }
  }

  /* ---------- Ano do rodapé ---------- */

  const year = document.getElementById("footer-year");
  if (year) {
    year.textContent = String(new Date().getFullYear());
  }
})();
