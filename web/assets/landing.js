/* PodStack — landing page.
   Único fetch do site: resolve a última release no GitHub para apontar os
   botões de download direto para o .rpm e o .deb publicados. Se a API falhar
   (offline, rate limit ou bloqueio), os links continuam em /releases/latest. */
(() => {
  "use strict";

  const API = "https://api.github.com/repos/marcos2872/podstack/releases/latest";
  const CACHE_KEY = "podstack-latest-release";
  const CACHE_TTL = 10 * 60 * 1000;

  const targets = document.querySelectorAll("[data-release], [data-release-file], [data-latest-version]");
  if (targets.length === 0) {
    return;
  }

  function readCache() {
    try {
      const raw = window.sessionStorage.getItem(CACHE_KEY);
      if (!raw) {
        return null;
      }
      const cached = JSON.parse(raw);
      if (!cached || typeof cached.at !== "number" || Date.now() - cached.at > CACHE_TTL) {
        return null;
      }
      return cached.data;
    } catch (_) {
      return null;
    }
  }

  function writeCache(data) {
    try {
      window.sessionStorage.setItem(CACHE_KEY, JSON.stringify({ at: Date.now(), data }));
    } catch (_) {
      /* sessionStorage indisponível: o site continua funcionando. */
    }
  }

  function pickAsset(assets, extension) {
    return (
      assets.find(
        (asset) =>
          typeof asset.name === "string" &&
          asset.name.endsWith(extension) &&
          !/debug|debuginfo|debugsource/i.test(asset.name),
      ) || null
    );
  }

  function apply(release) {
    if (!release || typeof release !== "object") {
      return;
    }

    const version = String(release.tag_name || "").replace(/^v/, "");
    if (version) {
      document.querySelectorAll("[data-latest-version]").forEach((node) => {
        node.textContent = version;
      });
    }

    const assets = Array.isArray(release.assets) ? release.assets : [];
    [["rpm", ".rpm"], ["deb", ".deb"]].forEach(([kind, extension]) => {
      const asset = pickAsset(assets, extension);
      if (!asset || typeof asset.browser_download_url !== "string") {
        return;
      }
      document.querySelectorAll(`[data-release="${kind}"]`).forEach((link) => {
        link.href = asset.browser_download_url;
      });
      document.querySelectorAll(`[data-release-file="${kind}"]`).forEach((node) => {
        node.textContent = asset.name;
      });
    });
  }

  const cached = readCache();
  if (cached) {
    apply(cached);
  }

  window
    .fetch(API, { headers: { Accept: "application/vnd.github+json" } })
    .then((response) => (response.ok ? response.json() : null))
    .then((release) => {
      if (!release) {
        return;
      }
      writeCache(release);
      apply(release);
    })
    .catch(() => {
      /* Mantém os links de fallback para a página de releases. */
    });
})();
