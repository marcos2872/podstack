/* PodStack — UI concept (dados mock + navegação) */
(() => {
  "use strict";

  /* ------------------------------------------------------------------ icons */
  const ICONS = {
    containers: '<path d="M21 8.5 12 3 3 8.5l9 5.5 9-5.5Z"/><path d="M3 8.5V16l9 5 9-5V8.5"/><path d="M12 14v7"/>',
    images: '<path d="m12 3 9 5-9 5-9-5 9-5Z"/><path d="m3 12 9 5 9-5"/><path d="m3 16 9 5 9-5"/>',
    volumes: '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v7c0 1.7 3.6 3 8 3s8-1.3 8-3V5"/><path d="M4 12v7c0 1.7 3.6 3 8 3s8-1.3 8-3v-7"/>',
    networks: '<circle cx="6" cy="12" r="2.5"/><circle cx="18" cy="6" r="2.5"/><circle cx="18" cy="18" r="2.5"/><path d="m8.3 10.8 7.4-3.6M8.3 13.2l7.4 3.6"/>',
    compose: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    builds: '<path d="m14 4 6 6"/><path d="M3 21l7.5-7.5"/><path d="m10 10 4-4 4 4-4 4-4-4Z"/>',
    pods: '<path d="M12 2 21 7v10l-9 5-9-5V7l9-5Z"/><path d="M12 12v10M3 7l9 5 9-5"/>',
    machines: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
    devices: '<path d="M9 3v6M15 3v6"/><path d="M6 9h12v3a6 6 0 0 1-12 0V9Z"/><path d="M12 18v3"/>',
    settings: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1Z"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    refresh: '<path d="M20 12a8 8 0 1 1-2.3-5.7"/><path d="M20 4v5h-5"/>',
    play: '<path d="m8 5 11 7-11 7V5Z"/>',
    stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>',
    rotate: '<path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/>',
    trash: '<path d="M4 7h16M9 7V5h6v2M6 7l1 13h10l1-13"/>',
    terminal: '<path d="m4 6 6 6-6 6"/><path d="M12 18h8"/>',
    folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/>',
    file: '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9l-6-6Z"/><path d="M14 3v6h6"/>',
    cpu: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M1 9h3M1 15h3M20 9h3M20 15h3"/>',
    memory: '<rect x="3" y="7" width="18" height="10" rx="2"/><path d="M7 7v-3M12 7v-3M17 7v-3M7 17v3M12 17v3M17 17v3"/>',
    activity: '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    drive: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M7 15h.01M11 15h.01"/>',
    chevron: '<path d="m9 18 6-6-6-6"/>',
    x: '<path d="m6 6 12 12M18 6 6 18"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    moon: '<path d="M21 12.8A9 9 0 1 1 11.2 3 7 7 0 0 0 21 12.8Z"/>',
    check: '<path d="m5 12 5 5L20 7"/>',
    alert: '<path d="M12 3 2 21h20L12 3Z"/><path d="M12 9v5M12 18h.01"/>',
    copy: '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
    external: '<path d="M14 3h7v7"/><path d="M10 14 21 3"/><path d="M21 14v5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5"/>',
    more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
    upload: '<path d="M12 21V9"/><path d="m7 14 5-5 5 5"/><path d="M5 3h14"/>',
    download: '<path d="M12 3v12"/><path d="m7 10 5 5 5-5"/><path d="M5 21h14"/>',
    filter: '<path d="M3 5h18l-7 8v6l-4 2v-8L3 5Z"/>',
    database: '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v7c0 1.7 3.6 3 8 3s8-1.3 8-3V5"/><path d="M4 12v7c0 1.7 3.6 3 8 3s8-1.3 8-3v-7"/>',
    arrowUp: '<path d="M12 19V5M6 11l6-6 6 6"/>',
    arrowDown: '<path d="M12 5v14M6 13l6 6 6-6"/>',
    minimize: '<path d="M5 12h14"/>',
    maximize: '<rect x="5" y="5" width="14" height="14" rx="1.5"/>',
    restore: '<rect x="8" y="8" width="11" height="11" rx="1.5"/><path d="M5 15V6a1 1 0 0 1 1-1h9"/>',
    wifi: '<path d="M3 9a15 15 0 0 1 18 0"/><path d="M6 12a10 10 0 0 1 12 0"/><path d="M9 15a5 5 0 0 1 6 0"/><circle cx="12" cy="18" r=".8"/>',
    volume: '<path d="M11 5 6 9H3v6h3l5 4V5Z"/><path d="M15 9a5 5 0 0 1 0 6"/>',
    battery: '<rect x="2" y="7" width="18" height="10" rx="2"/><path d="M22 11v2"/><rect x="4" y="9" width="12" height="6" rx="1" fill="currentColor" stroke="none"/>',
    shield: '<path d="M12 3 4 6v6c0 5 3.5 8 8 9 4.5-1 8-4 8-9V6l-8-3Z"/><path d="m9 12 2 2 4-4"/>',
  };

  function icon(name, cls = "") {
    const path = ICONS[name] || ICONS.file;
    return `<svg class="${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${path}</svg>`;
  }

  /* ------------------------------------------------------------------ data */
  const DEFAULT_FILES = [
    { name: "app", dir: true, mode: "0755" },
    { name: "node_modules", dir: true, mode: "0755" },
    { name: "package.json", size: "1.2 KB", mode: "0644" },
    { name: "package-lock.json", size: "184 KB", mode: "0644" },
    { name: "dist", dir: true, mode: "0755" },
    { name: ".env", size: "512 B", mode: "0600" },
  ];

  const data = {
    containers: [
      {
        id: "api", name: "api", image: "ghcr.io/acme/api:1.18.2", state: "running",
        status: "Up 2 hours", health: "healthy", ports: "8080→3000", project: "acme",
        cpu: "2.4%", mem: "148 MB", net: "1,2 MB / 840 KB", disk: "0 B / 12 MB", pids: 24,
        ip: "10.89.0.4", created: "há 2 h", command: "node dist/main.js",
        env: ["NODE_ENV=production", "PORT=3000", "LOG_LEVEL=info", "DATABASE_URL=postgres://…"],
        mounts: ["/home/dev/acme/api → /app"],
        files: DEFAULT_FILES,
        logs: [
          ["12:31:04", "info", "Server listening on :3000"],
          ["12:31:04", "ok", "Connected to postgres:5432"],
          ["12:31:05", "info", "GET /health 200 2ms"],
          ["12:31:07", "info", "GET /api/projects 200 18ms"],
          ["12:31:12", "warn", "Slow query (312ms): SELECT * FROM projects"],
          ["12:31:18", "info", "GET /api/projects 200 21ms"],
        ],
      },
      {
        id: "web", name: "web", image: "ghcr.io/acme/web:0.9.4", state: "running",
        status: "Up 2 hours", health: "healthy", ports: "80→80, 443→443", project: "acme",
        cpu: "0.8%", mem: "42 MB", net: "3,4 MB / 2,1 MB", disk: "0 B / 4 MB", pids: 12,
        ip: "10.89.0.5", created: "há 2 h", command: "nginx -g 'daemon off;'",
        env: ["NGINX_HOST=acme.localhost"], mounts: [],
        files: [{ name: "etc", dir: true, mode: "0755" }, { name: "usr", dir: true, mode: "0755" }, { name: "var", dir: true, mode: "0755" }],
        logs: [
          ["12:30:58", "info", "nginx/1.27.0 started"],
          ["12:31:05", "info", "172.17.0.1 - GET / 200 1.2ms"],
          ["12:31:11", "info", "172.17.0.1 - GET /assets/app.js 200 0.8ms"],
        ],
      },
      {
        id: "postgres", name: "postgres", image: "postgres:16-alpine", state: "running",
        status: "Up 2 hours", health: "healthy", ports: "5432→5432", project: "acme",
        cpu: "1.1%", mem: "212 MB", net: "840 KB / 1,1 MB", disk: "312 MB / 312 MB", pids: 9,
        ip: "10.89.0.2", created: "há 2 h", command: "postgres",
        env: ["POSTGRES_DB=acme", "POSTGRES_USER=acme", "PGDATA=/var/lib/postgresql/data"],
        mounts: ["acme_pgdata → /var/lib/postgresql/data"],
        files: [{ name: "var", dir: true, mode: "0755" }, { name: "docker-entrypoint.sh", size: "3.1 KB", mode: "0755" }],
        logs: [
          ["12:30:51", "info", "database system is ready to accept connections"],
          ["12:31:04", "info", "connection authorized: user=acme database=acme"],
          ["12:31:12", "info", "duration: 312.441 ms  statement: SELECT * FROM projects"],
        ],
      },
      {
        id: "redis", name: "redis", image: "redis:7-alpine", state: "running",
        status: "Up 2 hours", health: "—", ports: "6379→6379", project: "acme",
        cpu: "0.2%", mem: "12 MB", net: "128 KB / 96 KB", disk: "0 B / 1 MB", pids: 6,
        ip: "10.89.0.3", created: "há 2 h", command: "redis-server",
        env: ["REDIS_ARGS=--appendonly yes"], mounts: ["acme_redis → /data"],
        files: [{ name: "data", dir: true, mode: "0755" }, { name: "usr", dir: true, mode: "0755" }],
        logs: [
          ["12:30:50", "info", "Ready to accept connections tcp"],
          ["12:31:02", "info", "1:M 08 Nov 2026 12:31:02.117 * Background saving started"],
        ],
      },
      {
        id: "worker", name: "worker", image: "ghcr.io/acme/worker:0.4.0", state: "stopped",
        status: "Exited (0) 12 min", health: "—", ports: "—", project: "acme",
        cpu: "0.0%", mem: "0 B", net: "0 B / 0 B", disk: "0 B / 8 MB", pids: 0,
        ip: "—", created: "há 2 h", command: "node dist/worker.js",
        env: ["QUEUE_URL=redis://redis:6379"], mounts: [],
        files: [{ name: "app", dir: true, mode: "0755" }, { name: "package.json", size: "980 B", mode: "0644" }],
        logs: [["12:18:44", "ok", "worker stopped gracefully (SIGTERM)"]],
      },
      {
        id: "mailhog", name: "mailhog", image: "mailhog/mailhog:latest", state: "stopped",
        status: "Exited (1) 1 h", health: "—", ports: "8025→8025", project: "sandbox",
        cpu: "0.0%", mem: "0 B", net: "0 B / 0 B", disk: "0 B / 3 MB", pids: 0,
        ip: "—", created: "há 1 h", command: "MailHog",
        env: [], mounts: [],
        files: [{ name: "MailHog", size: "18 MB", mode: "0755" }],
        logs: [["11:29:02", "err", "failed to bind :1025: address already in use"], ["11:29:02", "err", "exit status 1"]],
      },
    ],
    machines: [
      {
        id: "dev", name: "dev", distro: "Ubuntu", version: "24.04", arch: "arm64",
        cpus: 6, memory: "8 GB", disk: "64 GB", status: "Running", state: "running",
        ip: "192.168.215.2", ssh: "ssh dev", sshPort: 2222, user: "dev",
        created: "há 3 dias", uptime: "3d 4h", kernel: "6.8.0-45-generic", qemu: "QEMU 9.0.2",
        cpuPct: 12, memPct: 39, diskPct: 28,
        mounts: ["/home/dev → /Users/dev"], ports: ["2222→22"],
        logs: [
          ["0.000000", "Linux version 6.8.0-45-generic (buildd@...) ..."],
          ["0.412318", "cloud-init: finished successfully"],
          ["1.902441", "sshd: Server listening on 0.0.0.0 port 22"],
          ["2.104882", "virtiofs: mounted /Users/dev"],
        ],
      },
      {
        id: "k8s", name: "k8s", distro: "Fedora", version: "40", arch: "arm64",
        cpus: 4, memory: "4 GB", disk: "32 GB", status: "Stopped", state: "stopped",
        ip: "—", ssh: "ssh k8s", sshPort: 2223, user: "fedora",
        created: "há 1 semana", uptime: "—", kernel: "6.9.5-200.fc40.aarch64", qemu: "QEMU 9.0.2",
        cpuPct: 0, memPct: 0, diskPct: 46,
        mounts: [], ports: ["2223→22"],
        logs: [["—", "machine stopped by user"]],
      },
      {
        id: "ubuntu-test", name: "ubuntu-test", distro: "Ubuntu", version: "22.04", arch: "amd64",
        cpus: 2, memory: "2 GB", disk: "16 GB", status: "Stopped", state: "stopped",
        ip: "—", ssh: "ssh ubuntu-test", sshPort: 2224, user: "ubuntu",
        created: "há 2 semanas", uptime: "—", kernel: "5.15.0-119-generic", qemu: "QEMU 9.0.2",
        cpuPct: 0, memPct: 0, diskPct: 18,
        mounts: [], ports: ["2224→22"],
        logs: [["—", "machine stopped by user"]],
      },
    ],
    devices: [
      { id: "usb-1", type: "USB", name: "YubiKey 5C", vidpid: "1050:0407", status: "Attached", machine: "dev", bus: "003", port: "002" },
      { id: "usb-2", type: "USB", name: "Logitech USB Receiver", vidpid: "046d:c52b", status: "Available", machine: "—", bus: "001", port: "004" },
      { id: "serial-1", type: "Serial", name: "/dev/ttyUSB0", vidpid: "10c4:ea60", status: "Attached", machine: "k8s", bus: "—", port: "—" },
      { id: "usb-3", type: "USB", name: "SanDisk Ultra", vidpid: "0781:5581", status: "Available", machine: "—", bus: "002", port: "001" },
      { id: "usb-4", type: "USB", name: "Apple Keyboard", vidpid: "05ac:024f", status: "Error", machine: "—", bus: "001", port: "006", error: "device busy" },
    ],
    images: [
      { repo: "ghcr.io/acme/api", tag: "1.18.2", digest: "sha256:9f2c…a1b7", size: "182 MB", created: "há 3 dias", used: 1 },
      { repo: "ghcr.io/acme/web", tag: "0.9.4", digest: "sha256:4d8e…c2f0", size: "64 MB", created: "há 3 dias", used: 1 },
      { repo: "ghcr.io/acme/worker", tag: "0.4.0", digest: "sha256:7a1b…9e3c", size: "156 MB", created: "há 5 dias", used: 1 },
      { repo: "postgres", tag: "16-alpine", digest: "sha256:b3f1…7d42", size: "243 MB", created: "há 1 semana", used: 1 },
      { repo: "redis", tag: "7-alpine", digest: "sha256:1c9e…4a88", size: "41 MB", created: "há 2 semanas", used: 1 },
      { repo: "nginx", tag: "1.27-alpine", digest: "sha256:8e4d…0f21", size: "48 MB", created: "há 5 dias", used: 1 },
      { repo: "<none>", tag: "<none>", digest: "sha256:2f7a…b6d1", size: "1.2 GB", created: "há 1 mês", used: 0, dangling: true },
    ],
    volumes: [
      { name: "acme_pgdata", driver: "local", size: "312 MB", links: 1, mount: "/var/lib/containers/storage/volumes/acme_pgdata/_data" },
      { name: "acme_redis", driver: "local", size: "18 MB", links: 1, mount: "/var/lib/containers/storage/volumes/acme_redis/_data" },
      { name: "acme_uploads", driver: "local", size: "1.8 GB", links: 1, mount: "/var/lib/containers/storage/volumes/acme_uploads/_data" },
      { name: "cache", driver: "local", size: "24 MB", links: 0, mount: "/var/lib/containers/storage/volumes/cache/_data" },
    ],
    networks: [
      { name: "podman", driver: "bridge", subnet: "10.88.0.0/16", containers: 2 },
      { name: "acme_default", driver: "bridge", subnet: "10.89.0.0/24", containers: 4 },
      { name: "sandbox_default", driver: "bridge", subnet: "10.91.0.0/24", containers: 1 },
    ],
    compose: [
      { name: "acme", dir: "/home/dev/acme", status: "running", services: ["api", "web", "postgres", "redis"], config: "compose.yaml" },
      { name: "observability", dir: "/home/dev/observability", status: "partial", services: ["grafana", "prometheus", "loki"], config: "docker-compose.yml" },
      { name: "sandbox", dir: "/home/dev/sandbox", status: "stopped", services: ["playground"], config: "compose.yaml" },
    ],
    builds: [
      { id: "b-1042", image: "ghcr.io/acme/api:1.18.2", status: "success", duration: "42.1s", started: "há 2 h", trigger: "CLI", context: "/home/dev/acme/api", dockerfile: "Dockerfile" },
      { id: "b-1041", image: "ghcr.io/acme/web:0.9.4", status: "success", duration: "1m 08s", started: "há 3 h", trigger: "Compose", context: "/home/dev/acme/web", dockerfile: "Dockerfile" },
      { id: "b-1040", image: "ghcr.io/acme/api:1.18.1", status: "failed", duration: "12.4s", started: "ontem", trigger: "GUI", context: "/home/dev/acme/api", dockerfile: "Dockerfile" },
      { id: "b-1039", image: "ghcr.io/acme/worker:0.4.0", status: "cancelled", duration: "8.2s", started: "ontem", trigger: "CLI", context: "/home/dev/acme/worker", dockerfile: "Dockerfile" },
    ],
    pods: [
      { name: "acme", status: "Running", containers: 4, id: "a1b2c3d4", ip: "10.88.0.10", age: "há 2 h" },
      { name: "observability", status: "Degraded", containers: 3, id: "e5f6a7b8", ip: "10.88.0.11", age: "há 5 h" },
    ],
  };

  const state = {
    view: "containers",
    theme: "dark",
    search: "",
    containerFilter: "all",
    selectedContainer: "api",
    inspectorTab: "info",
    selectedBuild: "b-1042",
    selectedMachine: "dev",
    cpuHistory: [],
    cpuHistoryFor: null,
    memHistory: [],
    memHistoryFor: null,
    modalConfirm: null,
    statsTimer: null,
    setup: {
      open: false,
      step: 0,
      busy: false,
      progress: 0,
      logs: [],
      docker: true,
      podman: false,
      podmanVersion: "",
      podmanLatest: "5.8.9",
      podmanOutdated: false,
      qemu: false,
      kvm: true,
      virtiofsd: false,
      libvirt: false,
      caddy: false,
      k3s: false,
      sqlite: true,
      socket: false,
      rootless: false,
      installK3s: false,
      scanning: false,
      validating: false,
      validated: false,
    },
  };

  const VIEWS = {
    containers: "Containers",
    images: "Images",
    volumes: "Volumes",
    networks: "Networks",
    compose: "Compose",
    builds: "Builds",
    pods: "Pods",
    machines: "Machines",
    devices: "Devices",
  };

  const SETUP_STEPS = [
    { label: "Boas-vindas", hint: "visão geral" },
    { label: "Diagnóstico", hint: "o que já existe" },
    { label: "Docker → Podman", hint: "sem conflito de engine" },
    { label: "QEMU/KVM", hint: "máquinas Linux" },
    { label: "Componentes", hint: "socket, Caddy, k3s" },
    { label: "Validação", hint: "checklist final" },
    { label: "Pronto", hint: "abrir o app" },
  ];

  const NAV = [
    {
      label: "Docker",
      items: [
        { id: "containers", label: "Containers", icon: "containers", count: () => data.containers.length },
        { id: "images", label: "Images", icon: "images", count: () => data.images.length },
        { id: "volumes", label: "Volumes", icon: "volumes", count: () => data.volumes.length },
        { id: "networks", label: "Networks", icon: "networks", count: () => data.networks.length },
        { id: "compose", label: "Compose", icon: "compose", count: () => data.compose.length },
        { id: "builds", label: "Builds", icon: "builds", count: () => data.builds.length },
      ],
    },
    {
      label: "Kubernetes",
      items: [{ id: "pods", label: "Pods", icon: "pods", count: () => data.pods.length }],
    },
    {
      label: "Linux",
      items: [
        { id: "machines", label: "Machines", icon: "machines", count: () => data.machines.length },
        { id: "devices", label: "Devices", icon: "devices", count: () => data.devices.length },
      ],
    },
  ];

  /* ------------------------------------------------------------------ helpers */
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

  function escapeHtml(value) {
    return String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  function runningCount() {
    return data.containers.filter((c) => c.state === "running").length;
  }

  function selectedContainer() {
    return data.containers.find((c) => c.id === state.selectedContainer) || data.containers[0];
  }

  function matches(text) {
    if (!state.search) return true;
    return String(text).toLowerCase().includes(state.search.toLowerCase());
  }

  function now() {
    return new Date().toLocaleTimeString("pt-BR", { hour12: false });
  }

  function logLine(ts, level, msg) {
    return `<div class="log-line"><span class="log-ts">${escapeHtml(ts)}</span><span class="log-level ${level}">${level.toUpperCase()}</span><span class="log-msg">${escapeHtml(msg)}</span></div>`;
  }

  /* ------------------------------------------------------------------ sidebar */
  function renderSidebar() {
    const groups = NAV.map((group) => `
      <div class="nav-group">
        <div class="nav-label">${group.label}</div>
        ${group.items.map((item) => {
          if (item.disabled) {
            return `<div class="nav-item" data-action="soon" data-label="${item.label}" aria-disabled="true">${icon(item.icon, "nav-icon")}<span class="nav-text">${item.label}</span></div>`;
          }
          const active = state.view === item.id ? "active" : "";
          return `<div class="nav-item ${active}" data-view="${item.id}" role="button" tabindex="0">${icon(item.icon, "nav-icon")}<span class="nav-text">${item.label}</span>${item.count ? `<span class="nav-count">${item.count()}</span>` : ""}</div>`;
        }).join("")}
      </div>`).join("");

    $("#sidebar").innerHTML = `
      <div class="brand">
        <div class="logo" aria-hidden="true"></div>
        <div><div class="brand-name">PodStack</div><div class="brand-version">v0.1.0 · UI concept</div></div>
      </div>
      ${groups}
      <div class="sidebar-foot">
        <div class="sidebar-hr"></div>
        <div class="nav-item" data-action="settings" role="button" tabindex="0">${icon("settings", "nav-icon")}<span class="nav-text">Settings</span></div>
      </div>`;
  }

  /* ------------------------------------------------------------------ topbar */
  function renderTopbar() {
    $("#topbar").innerHTML = `
      <div class="topbar-left">
        <span class="crumb">PodStack</span><span class="crumb">/</span><span class="crumb"><b>${VIEWS[state.view]}</b></span>
      </div>
      <div class="topbar-right">
        <div class="search">${icon("search")}<input id="search" type="search" placeholder="Buscar containers, imagens, projetos…" value="${escapeHtml(state.search)}" aria-label="Buscar" /></div>
        <button class="btn icon" data-action="theme" title="Alternar tema">${icon(state.theme === "dark" ? "sun" : "moon")}</button>
        <span class="engine-pill"><span class="dot running"></span> rootless</span>
      </div>`;

    const input = $("#search");
    input.addEventListener("input", (event) => {
      state.search = event.target.value;
      renderMain();
    });
  }

  /* ------------------------------------------------------------------ containers */
  function containerRow(c) {
    const selected = c.id === state.selectedContainer ? "selected" : "";
    const health = c.health === "healthy" ? '<span class="badge success">healthy</span>' : "";
    return `
      <div class="row ${selected}" data-container="${c.id}" role="button" tabindex="0">
        <span class="dot ${c.state === "running" ? "running" : c.health === "unhealthy" ? "danger" : "stopped"}"></span>
        <div class="row-main">
          <div class="row-name">${escapeHtml(c.name)} ${health} <span class="badge muted">${escapeHtml(c.project)}</span></div>
          <div class="row-meta"><span>${escapeHtml(c.image)}</span>${c.ports ? `<span class="sep">·</span><span>${escapeHtml(c.ports)}</span>` : ""}</div>
        </div>
        <div class="row-status">
          <span>${escapeHtml(c.status)}</span>
          <span class="row-actions">
            ${c.state === "running"
              ? `<button class="btn icon sm" data-action="stop" data-id="${c.id}" title="Parar">${icon("stop")}</button>`
              : `<button class="btn icon sm" data-action="start" data-id="${c.id}" title="Iniciar">${icon("play")}</button>`}
            <button class="btn icon sm" data-action="restart" data-id="${c.id}" title="Reiniciar">${icon("rotate")}</button>
          </span>
        </div>
      </div>`;
  }

  function containersView() {
    const filtered = data.containers.filter((c) => {
      const text = `${c.name} ${c.image} ${c.project}`;
      const bySearch = matches(text);
      const f = state.containerFilter;
      const byFilter = f === "all" || (f === "running" && c.state === "running") || (f === "stopped" && c.state !== "running");
      return bySearch && byFilter;
    });
    const selected = selectedContainer();
    const running = runningCount();
    const stopped = data.containers.length - running;

    return `
      <div class="view-head">
        <div>
          <div class="view-title">Containers</div>
          <div class="view-sub">${data.containers.length} containers · <span style="color:var(--success)">${running} em execução</span> · ${stopped} parados</div>
        </div>
        <div class="view-actions">
          <button class="btn primary" data-action="new-container">${icon("plus")} Novo container</button>
          <button class="btn" data-action="start-all">${icon("play")} Start all</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <div class="toolbar">
        <div class="tabs" role="tablist">
          <button class="tab ${state.containerFilter === "all" ? "active" : ""}" data-filter="all">Todos <span class="count">${data.containers.length}</span></button>
          <button class="tab ${state.containerFilter === "running" ? "active" : ""}" data-filter="running">Em execução <span class="count">${running}</span></button>
          <button class="tab ${state.containerFilter === "stopped" ? "active" : ""}" data-filter="stopped">Parados <span class="count">${stopped}</span></button>
        </div>
        <span class="grow"></span>
        <span class="muted" style="font-size:10.5px">${filtered.length} de ${data.containers.length}</span>
      </div>
      <div class="containers-grid">
        <section class="panel list-panel">
          <div class="panel-head">
            <span class="panel-title">Containers</span>
            <span class="panel-sub">agrupados por projeto</span>
            <span class="grow"></span>
            <button class="btn ghost sm" data-action="filter-toggle">${icon("filter")} Filtrar</button>
          </div>
          <div class="list-scroll">${filtered.map(containerRow).join("") || `<div class="empty"><b>Nenhum resultado</b>Tente outro filtro ou termo de busca.</div>`}</div>
        </section>
        ${inspectorHtml(selected)}
      </div>`;
  }

  function meta(label, value) {
    return `<div class="meta"><div class="meta-label">${label}</div><div class="meta-value">${value || "—"}</div></div>`;
  }

  function inspectorBody(c) {
    if (state.inspectorTab === "logs") return logsBody(c);
    if (state.inspectorTab === "stats") return statsBody(c);
    if (state.inspectorTab === "files") return filesBody(c);
    return `
      <div class="meta-grid">
        ${meta("ID", `<span class="mono">${escapeHtml(c.id)}</span>`)}
        ${meta("Projeto", escapeHtml(c.project))}
        ${meta("IP", `<span class="mono">${escapeHtml(c.ip)}</span>`)}
        ${meta("Criado", escapeHtml(c.created))}
        ${meta("Portas", escapeHtml(c.ports))}
        ${meta("Comando", `<span class="mono">${escapeHtml(c.command)}</span>`)}
      </div>
      <div class="section-label">Saúde</div>
      <div class="chips">${c.health === "healthy" ? '<span class="badge success">healthy</span>' : '<span class="badge muted">sem healthcheck</span>'}</div>
      <div class="section-label">Variáveis</div>
      <div class="code-block">${c.env.length ? c.env.map((e) => {
        const i = e.indexOf("=");
        return `<div class="code-line"><span class="code-key">${escapeHtml(e.slice(0, i))}</span>=${escapeHtml(e.slice(i + 1))}</div>`;
      }).join("") : '<span class="muted">nenhuma</span>'}</div>
      <div class="section-label">Mounts</div>
      <div class="chips">${c.mounts.length ? c.mounts.map((m) => `<span class="chip">${escapeHtml(m)}</span>`).join("") : '<span class="muted">nenhum</span>'}</div>`;
  }

  function statsBody(c) {
    return `
      <div class="stat-grid">
        <div class="stat-card"><div class="stat-label">CPU</div><div class="stat-value accent" data-stat="cpu">${c.cpu}</div><div class="stat-foot">12 núcleos</div></div>
        <div class="stat-card"><div class="stat-label">Memória</div><div class="stat-value" data-stat="mem">${c.mem}</div><div class="stat-foot">de 2 GB</div></div>
        <div class="stat-card"><div class="stat-label">Rede (RX / TX)</div><div class="stat-value" data-stat="net">${c.net}</div><div class="stat-foot">eth0</div></div>
        <div class="stat-card"><div class="stat-label">Disco / PIDs</div><div class="stat-value" data-stat="disk">${c.disk}</div><div class="stat-foot"><span data-stat="pids">${c.pids}</span> processos</div></div>
      </div>
      <div class="charts">
        <div class="spark-wrap">
          <div class="spark-head"><span>CPU (%) — últimas amostras</span><span data-stat="cpu-now">${c.cpu}</span></div>
          <svg class="spark" id="spark" viewBox="0 0 300 64" preserveAspectRatio="none" aria-hidden="true">
            <polygon id="sparkArea" fill="rgba(34,211,238,.10)" points=""></polygon>
            <polyline id="sparkLine" fill="none" stroke="#22d3ee" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round" points=""></polyline>
          </svg>
        </div>
        <div class="spark-wrap">
          <div class="spark-head"><span>RAM (MB) — últimas amostras</span><span data-stat="mem-now">${c.mem}</span></div>
          <svg class="spark" id="memSpark" viewBox="0 0 300 64" preserveAspectRatio="none" aria-hidden="true">
            <polygon id="memSparkArea" fill="rgba(129,140,248,.10)" points=""></polygon>
            <polyline id="memSparkLine" fill="none" stroke="#818cf8" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round" points=""></polyline>
          </svg>
        </div>
      </div>`;
  }

  function logsBody(c) {
    return `
      <div class="logs-toolbar">
        <button class="btn sm">${icon("search")} Filtrar</button>
        <span class="badge muted">seguir</span>
        <span class="grow"></span>
        <button class="btn sm" data-action="clear-logs">Limpar</button>
      </div>
      <div class="terminal" id="terminal">
        ${c.logs.map(([ts, level, msg]) => logLine(ts, level, msg)).join("")}
        <div class="log-line"><span class="log-ts">${now()}</span><span class="log-level ok">$</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div>
      </div>`;
  }

  function filesBody(c) {
    return `
      <div class="logs-toolbar">
        <span class="mono muted">/</span>
        <span class="grow"></span>
        <button class="btn sm" data-action="upload">${icon("upload")} Enviar</button>
      </div>
      <div>${c.files.map((f) => `
        <div class="file-row" data-action="file" data-name="${escapeHtml(f.name)}">
          <span class="file-icon">${icon(f.dir ? "folder" : "file")}</span>
          <span class="file-name">${escapeHtml(f.name)}</span>
          <span class="file-size">${escapeHtml(f.size || "")}</span>
          <span class="file-mode">${escapeHtml(f.mode || "")}</span>
        </div>`).join("")}</div>`;
  }

  function inspectorHtml(c) {
    if (!c) return '<aside class="panel inspector"><div class="empty"><b>Nenhum container</b>Selecione um item na lista.</div></aside>';
    const tabs = [["info", "Info"], ["logs", "Logs"], ["stats", "Stats"], ["files", "Files"]];
    return `
      <aside class="panel inspector">
        <div class="inspector-head">
          <div class="inspector-title">${escapeHtml(c.name)} <span class="badge ${c.state === "running" ? "success" : "muted"}">${c.state}</span></div>
          <div class="inspector-sub">${escapeHtml(c.image)}</div>
          <div class="inspector-actions">
            ${c.state === "running"
              ? `<button class="btn sm" data-action="stop" data-id="${c.id}">${icon("stop")} Parar</button><button class="btn sm" data-action="restart" data-id="${c.id}">${icon("rotate")} Reiniciar</button>`
              : `<button class="btn sm primary" data-action="start" data-id="${c.id}">${icon("play")} Iniciar</button>`}
            <button class="btn sm" data-action="terminal" data-id="${c.id}">${icon("terminal")} Terminal</button>
            <button class="btn sm danger" data-action="remove" data-id="${c.id}" title="Remover">${icon("trash")}</button>
          </div>
        </div>
        <div class="segmented" role="tablist">${tabs.map(([id, label]) => `<button class="${state.inspectorTab === id ? "active" : ""}" data-tab="${id}">${label}</button>`).join("")}</div>
        <div class="inspector-body">${inspectorBody(c)}</div>
      </aside>`;
  }

  /* ------------------------------------------------------------------ images */
  function imagesView() {
    const rows = data.images.filter((i) => matches(`${i.repo}:${i.tag} ${i.digest}`));
    return `
      <div class="view-head">
        <div><div class="view-title">Images</div><div class="view-sub">${data.images.length} imagens · ${data.images.filter((i) => i.used > 0).length} em uso</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="pull">${icon("download")} Pull</button>
          <button class="btn" data-action="soon" data-label="Importar">${icon("upload")} Importar</button>
          <button class="btn" data-action="prune-images">${icon("trash")} Prune</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <section class="panel">
        <div class="panel-head"><span class="panel-title">Repositórios</span><span class="panel-sub">pull, tag e prune</span><span class="grow"></span><span class="badge muted">${rows.length} de ${data.images.length}</span></div>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Repositório:tag</th><th>Digest</th><th>Tamanho</th><th>Criada</th><th>Uso</th><th></th></tr></thead>
          <tbody>${rows.map((i) => `
            <tr>
              <td><div class="repo">${escapeHtml(i.repo)}<span class="muted">:${escapeHtml(i.tag)}</span> ${i.dangling ? '<span class="badge warning">dangling</span>' : ""}</div><div class="digest">${escapeHtml(i.digest)}</div></td>
              <td class="mono muted">${escapeHtml(i.digest.slice(0, 19))}…</td>
              <td>${escapeHtml(i.size)}</td>
              <td class="muted">${escapeHtml(i.created)}</td>
              <td>${i.used > 0 ? `<span class="badge accent">${i.used} container${i.used > 1 ? "s" : ""}</span>` : '<span class="badge muted">livre</span>'}</td>
              <td class="actions-cell"><button class="btn sm" data-action="remove-image" data-id="${i.repo}:${i.tag}">${icon("trash")}</button></td>
            </tr>`).join("") || `<tr><td colspan="6"><div class="empty"><b>Nenhuma imagem</b>Faça um pull para começar.</div></td></tr>`}</tbody>
        </table></div>
      </section>`;
  }

  /* ------------------------------------------------------------------ volumes */
  function volumesView() {
    const rows = data.volumes.filter((v) => matches(`${v.name} ${v.driver}`));
    return `
      <div class="view-head">
        <div><div class="view-title">Volumes</div><div class="view-sub">${data.volumes.length} volumes · ${data.volumes.reduce((n, v) => n + (v.links > 0 ? 1 : 0), 0)} em uso</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="create-volume">${icon("plus")} Criar</button>
          <button class="btn" data-action="prune-volumes">${icon("trash")} Prune</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <section class="panel">
        <div class="panel-head"><span class="panel-title">Volumes</span><span class="panel-sub">dados persistentes</span><span class="grow"></span><span class="badge muted">${rows.length} de ${data.volumes.length}</span></div>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Nome</th><th>Driver</th><th>Tamanho</th><th>Links</th><th>Mountpoint</th><th></th></tr></thead>
          <tbody>${rows.map((v) => `
            <tr>
              <td><span class="repo">${escapeHtml(v.name)}</span></td>
              <td class="muted">${escapeHtml(v.driver)}</td>
              <td>${escapeHtml(v.size)}</td>
              <td>${v.links > 0 ? `<span class="badge accent">${v.links}</span>` : '<span class="badge muted">0</span>'}</td>
              <td class="mono muted" style="max-width:280px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${escapeHtml(v.mount)}</td>
              <td class="actions-cell"><button class="btn sm" data-action="remove-volume" data-id="${v.name}">${icon("trash")}</button></td>
            </tr>`).join("") || `<tr><td colspan="6"><div class="empty"><b>Nenhum volume</b>Crie um volume para persistir dados.</div></td></tr>`}</tbody>
        </table></div>
      </section>`;
  }

  /* ------------------------------------------------------------------ networks */
  function networksView() {
    const rows = data.networks.filter((n) => matches(`${n.name} ${n.subnet}`));
    return `
      <div class="view-head">
        <div><div class="view-title">Networks</div><div class="view-sub">${data.networks.length} redes · bridge</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="create-network">${icon("plus")} Criar</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <section class="panel">
        <div class="panel-head"><span class="panel-title">Redes</span><span class="panel-sub">subnet, gateway e containers</span></div>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Nome</th><th>Driver</th><th>Subnet</th><th>Containers</th><th></th></tr></thead>
          <tbody>${rows.map((n) => `
            <tr>
              <td><span class="repo">${escapeHtml(n.name)}</span></td>
              <td class="muted">${escapeHtml(n.driver)}</td>
              <td class="mono">${escapeHtml(n.subnet)}</td>
              <td><span class="badge ${n.containers ? "accent" : "muted"}">${n.containers}</span></td>
              <td class="actions-cell"><button class="btn sm" data-action="remove-network" data-id="${n.name}">${icon("trash")}</button></td>
            </tr>`).join("") || `<tr><td colspan="5"><div class="empty"><b>Nenhuma rede</b>Crie uma rede bridge.</div></td></tr>`}</tbody>
        </table></div>
      </section>`;
  }

  /* ------------------------------------------------------------------ compose */
  function composeView() {
    const rows = data.compose.filter((p) => matches(`${p.name} ${p.dir} ${p.services.join(" ")}`));
    return `
      <div class="view-head">
        <div><div class="view-title">Compose</div><div class="view-sub">${data.compose.length} projetos · ${data.compose.filter((p) => p.status === "running").length} ativos</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="compose-up">${icon("play")} Up por diretório</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <div class="compose-grid">${rows.map((p) => `
        <section class="panel compose-card">
          <div class="compose-head">
            <div style="min-width:0">
              <div class="compose-name"><span class="dot ${p.status === "running" ? "running" : p.status === "partial" ? "warning" : "stopped"}"></span>${escapeHtml(p.name)}</div>
              <div class="compose-dir">${escapeHtml(p.dir)}</div>
            </div>
            <span class="badge ${p.status === "running" ? "success" : p.status === "partial" ? "warning" : "muted"}">${p.status}</span>
          </div>
          <div class="service-list">${p.services.map((s) => `<span class="chip">${escapeHtml(s)}</span>`).join("")}</div>
          <div class="compose-actions">
            <button class="btn sm" data-action="compose-up" data-id="${p.name}">${icon("play")} Up</button>
            <button class="btn sm" data-action="compose-restart" data-id="${p.name}">${icon("rotate")} Restart</button>
            <button class="btn sm" data-action="compose-logs" data-id="${p.name}">${icon("terminal")} Logs</button>
            <span class="grow"></span>
            <button class="btn sm danger" data-action="compose-down" data-id="${p.name}">${icon("stop")}</button>
          </div>
          <div class="panel-foot">${p.services.length} serviços · ${escapeHtml(p.config)}</div>
        </section>`).join("") || `<div class="panel"><div class="empty"><b>Nenhum projeto</b>Use “Up por diretório”.</div></div>`}</div>`;
  }

  /* ------------------------------------------------------------------ builds */
  function buildLines(b) {
    if (b.status === "running") {
      return [
        ["10:42:01", "info", "STEP 1/5: FROM node:20-alpine"],
        ["10:42:02", "info", "STEP 2/5: WORKDIR /app"],
        ["10:42:03", "info", "STEP 3/5: COPY package*.json ./"],
        ["10:42:06", "info", "STEP 4/5: RUN npm ci --omit=dev"],
        ["10:42:18", "info", "npm ci in progress…"],
      ];
    }
    if (b.status === "failed") {
      return [
        ["10:42:01", "info", "STEP 1/5: FROM node:20-alpine"],
        ["10:42:02", "info", "STEP 2/5: WORKDIR /app"],
        ["10:42:03", "info", "STEP 3/5: COPY package*.json ./"],
        ["10:42:06", "info", "STEP 4/5: RUN npm ci --omit=dev"],
        ["10:42:12", "err", "npm ERR! code ERESOLVE"],
        ["10:42:12", "err", "npm ERR! ERESOLVE unable to resolve dependency tree"],
        ["10:42:12", "err", "exit status 1"],
      ];
    }
    if (b.status === "cancelled") {
      return [
        ["10:42:01", "info", "STEP 1/5: FROM node:20-alpine"],
        ["10:42:02", "info", "STEP 2/5: WORKDIR /app"],
        ["10:42:04", "warn", "build canceled by user"],
      ];
    }
    return [
      ["10:42:01", "info", "STEP 1/5: FROM node:20-alpine"],
      ["10:42:02", "info", "STEP 2/5: WORKDIR /app"],
      ["10:42:03", "info", "STEP 3/5: COPY package*.json ./"],
      ["10:42:06", "info", "STEP 4/5: RUN npm ci --omit=dev"],
      ["10:42:39", "info", "STEP 5/5: COPY . ."],
      ["10:42:41", "ok", "COMMIT ghcr.io/acme/api:1.18.2"],
      ["10:42:42", "ok", "Successfully tagged ghcr.io/acme/api:1.18.2"],
    ];
  }

  function buildsView() {
    const rows = data.builds.filter((b) => matches(`${b.id} ${b.image} ${b.trigger}`));
    const selected = data.builds.find((b) => b.id === state.selectedBuild) || data.builds[0];
    return `
      <div class="view-head">
        <div><div class="view-title">Builds</div><div class="view-sub">${data.builds.length} registros · histórico em SQLite</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="new-build">${icon("plus")} Novo build</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <div class="builds-grid">
        <section class="panel">
          <div class="panel-head"><span class="panel-title">Histórico</span><span class="panel-sub">imagem, status e duração</span></div>
          <div class="table-wrap"><table class="table">
            <thead><tr><th>Imagem</th><th>Status</th><th>Duração</th><th>Origem</th><th>Início</th><th></th></tr></thead>
            <tbody>${rows.map((b) => `
              <tr>
                <td><span class="repo">${escapeHtml(b.image)}</span><div class="digest">${escapeHtml(b.id)}</div></td>
                <td><span class="build-status ${b.status}"><span class="dot ${b.status === "success" ? "running" : b.status === "failed" ? "danger" : b.status === "running" ? "warning" : "stopped"}"></span>${b.status}</span></td>
                <td class="mono">${escapeHtml(b.duration)}</td>
                <td class="muted">${escapeHtml(b.trigger)}</td>
                <td class="muted">${escapeHtml(b.started)}</td>
                <td class="actions-cell"><button class="btn sm" data-action="select-build" data-id="${b.id}">${icon("terminal")} Log</button></td>
              </tr>`).join("") || `<tr><td colspan="6"><div class="empty"><b>Nenhum build</b>Inicie um novo build.</div></td></tr>`}</tbody>
          </table></div>
        </section>
        <aside class="panel">
          <div class="panel-head"><span class="panel-title">${escapeHtml(selected.id)}</span><span class="badge ${selected.status === "success" ? "success" : selected.status === "failed" ? "danger" : selected.status === "running" ? "warning" : "muted"}">${selected.status}</span><span class="grow"></span><button class="btn sm" data-action="remove-build" data-id="${selected.id}">${icon("trash")}</button></div>
          <div class="panel-body">
            <div class="meta-grid">
              ${meta("Imagem", `<span class="mono">${escapeHtml(selected.image)}</span>`)}
              ${meta("Duração", escapeHtml(selected.duration))}
              ${meta("Contexto", `<span class="mono">${escapeHtml(selected.context)}</span>`)}
              ${meta("Dockerfile", escapeHtml(selected.dockerfile))}
            </div>
            ${selected.status === "running" ? `<div class="progress"><div class="progress-bar" style="width:62%"></div></div>` : ""}
            <div class="section-label">Log</div>
            <div class="terminal build-log">${buildLines(selected).map(([ts, level, msg]) => logLine(ts, level, msg)).join("")}<div class="log-line"><span class="log-ts">${now()}</span><span class="log-level ok">$</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div></div>
          </div>
        </aside>
      </div>`;
  }

  /* ------------------------------------------------------------------ pods */
  function podsView() {
    const rows = data.pods.filter((p) => matches(`${p.name} ${p.id}`));
    return `
      <div class="view-head">
        <div><div class="view-title">Pods</div><div class="view-sub">${data.pods.length} pods · Kubernetes local</div></div>
        <div class="view-actions"><button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button></div>
      </div>
      <section class="panel">
        <div class="panel-head"><span class="panel-title">Pods</span><span class="panel-sub">infra containers e serviços</span></div>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Nome</th><th>Status</th><th>Containers</th><th>IP</th><th>Idade</th></tr></thead>
          <tbody>${rows.map((p) => `
            <tr>
              <td><span class="repo">${escapeHtml(p.name)}</span><div class="digest">${escapeHtml(p.id)}</div></td>
              <td><span class="badge ${p.status === "Running" ? "success" : "warning"}">${p.status}</span></td>
              <td>${p.containers}</td>
              <td class="mono">${escapeHtml(p.ip)}</td>
              <td class="muted">${escapeHtml(p.age)}</td>
            </tr>`).join("") || `<tr><td colspan="5"><div class="empty"><b>Nenhum pod</b>Crie um pod para agrupar containers.</div></td></tr>`}</tbody>
        </table></div>
      </section>`;
  }

  /* ------------------------------------------------------------------ machines */
  function selectedMachine() {
    return data.machines.find((m) => m.id === state.selectedMachine) || data.machines[0];
  }

  function machineStatusBadge(m) {
    const map = { Running: "success", Provisioning: "warning", Stopped: "muted", Error: "danger" };
    return `<span class="badge ${map[m.status] || "muted"}">${m.status}</span>`;
  }

  function resourceBar(label, value, pct) {
    return `<div class="resource">
      <div class="resource-head"><span>${label}</span><span>${value}</span></div>
      <div class="bar"><div class="bar-fill" style="width:${pct}%"></div></div>
      <div class="resource-foot">${pct}% em uso</div>
    </div>`;
  }

  function machineDetail(m) {
    if (!m) return '<aside class="panel inspector"><div class="empty"><b>Nenhuma máquina</b>Crie uma máquina para começar.</div></aside>';
    const running = m.state === "running";
    return `
      <aside class="panel inspector">
        <div class="inspector-head">
          <div class="inspector-title">${escapeHtml(m.name)} ${machineStatusBadge(m)}</div>
          <div class="inspector-sub">${escapeHtml(m.distro)} ${escapeHtml(m.version)} · ${escapeHtml(m.arch)}</div>
          <div class="inspector-actions">
            <button class="btn sm ${running ? "" : "primary"}" data-action="${running ? "machine-stop" : "machine-start"}" data-id="${m.id}">${icon(running ? "stop" : "play")} ${running ? "Parar" : "Iniciar"}</button>
            <button class="btn sm" data-action="machine-restart" data-id="${m.id}">${icon("rotate")} Reiniciar</button>
            <button class="btn sm" data-action="machine-terminal" data-id="${m.id}">${icon("terminal")} Terminal</button>
            <button class="btn sm" data-action="machine-ssh" data-id="${m.id}">${icon("external")} SSH</button>
          </div>
        </div>
        <div class="inspector-body">
          <div class="meta-grid">
            ${meta("IP", `<span class="mono">${escapeHtml(m.ip)}</span>`)}
            ${meta("SSH", `<span class="mono">${escapeHtml(m.ssh)}</span>`)}
            ${meta("Usuário", escapeHtml(m.user))}
            ${meta("Criada", escapeHtml(m.created))}
            ${meta("Uptime", escapeHtml(m.uptime))}
            ${meta("Kernel", `<span class="mono">${escapeHtml(m.kernel)}</span>`)}
          </div>
          <div class="section-label">Recursos</div>
          <div class="resource-grid">
            ${resourceBar("CPU", `${m.cpus} vCPU`, m.cpuPct)}
            ${resourceBar("Memória", m.memory, m.memPct)}
            ${resourceBar("Disco", m.disk, m.diskPct)}
          </div>
          <div class="section-label">SSH</div>
          <div class="code-block"><div class="code-line">${escapeHtml(m.ssh)} <button class="btn sm ghost" data-action="copy" data-value="${escapeHtml(m.ssh)}" title="Copiar" style="float:right;height:20px;padding:0 4px">${icon("copy")}</button></div></div>
          <div class="section-label">Mounts</div>
          <div class="chips">${m.mounts.length ? m.mounts.map((x) => `<span class="chip">${escapeHtml(x)}</span>`).join("") : '<span class="muted">nenhum</span>'}</div>
          <div class="section-label">Portas</div>
          <div class="chips">${m.ports.map((x) => `<span class="chip">${escapeHtml(x)}</span>`).join("")}</div>
          <div class="section-label">Console</div>
          <button class="btn sm" data-action="machine-logs" data-id="${m.id}">${icon("terminal")} Ver boot log</button>
          <div class="section-label">Zona de risco</div>
          <button class="btn sm danger" data-action="machine-delete" data-id="${m.id}">${icon("trash")} Remover máquina</button>
        </div>
      </aside>`;
  }

  function machinesView() {
    const rows = data.machines.filter((m) => matches(`${m.name} ${m.distro} ${m.version} ${m.arch}`));
    const selected = selectedMachine();
    const running = data.machines.filter((m) => m.state === "running").length;
    return `
      <div class="view-head">
        <div><div class="view-title">Machines</div><div class="view-sub">${data.machines.length} máquinas · <span style="color:var(--success)">${running} em execução</span> · QEMU/KVM</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="new-machine">${icon("plus")} Nova máquina</button>
          <button class="btn" data-action="soon" data-label="Importar">${icon("upload")} Importar</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <div class="containers-grid">
        <section class="panel list-panel">
          <div class="panel-head"><span class="panel-title">Máquinas</span><span class="panel-sub">cloud-init · virtiofs · SSH</span></div>
          <div class="table-wrap"><table class="table">
            <thead><tr><th>Nome</th><th>Distro</th><th>Status</th><th>Arch</th><th>Recursos</th><th>IP</th></tr></thead>
            <tbody>${rows.map((m) => `
              <tr data-machine="${m.id}" class="${m.id === state.selectedMachine ? "selected" : ""}">
                <td><span class="repo">${escapeHtml(m.name)}</span><div class="digest">${escapeHtml(m.qemu)}</div></td>
                <td>${escapeHtml(m.distro)} ${escapeHtml(m.version)}</td>
                <td>${machineStatusBadge(m)}</td>
                <td><span class="badge muted">${escapeHtml(m.arch)}</span></td>
                <td class="muted">${m.cpus} CPU · ${escapeHtml(m.memory)}</td>
                <td class="mono">${escapeHtml(m.ip)}</td>
              </tr>`).join("") || `<tr><td colspan="6"><div class="empty"><b>Nenhuma máquina</b>Crie uma máquina Linux.</div></td></tr>`}</tbody>
          </table></div>
        </section>
        ${machineDetail(selected)}
      </div>`;
  }

  /* ------------------------------------------------------------------ devices */
  function devicesView() {
    const rows = data.devices.filter((d) => matches(`${d.name} ${d.vidpid} ${d.type} ${d.machine}`));
    const attached = data.devices.filter((d) => d.status === "Attached").length;
    return `
      <div class="view-head">
        <div><div class="view-title">Devices</div><div class="view-sub">${data.devices.length} dispositivos · ${attached} conectados a máquinas</div></div>
        <div class="view-actions">
          <button class="btn primary" data-action="device-scan">${icon("refresh")} Escanear</button>
          <button class="btn icon" data-action="refresh" title="Atualizar">${icon("refresh")}</button>
        </div>
      </div>
      <div class="notice">
        ${icon("devices")}
        <div><b>Passthrough USB/serial</b><span>O dispositivo é removido do host enquanto estiver conectado a uma máquina. Desconecte antes de removê-lo fisicamente.</span></div>
      </div>
      <section class="panel">
        <div class="panel-head"><span class="panel-title">Dispositivos</span><span class="panel-sub">USB e serial</span><span class="grow"></span><span class="badge muted">${rows.length} de ${data.devices.length}</span></div>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Tipo</th><th>Nome</th><th>VID:PID</th><th>Status</th><th>Máquina</th><th>Barramento</th><th></th></tr></thead>
          <tbody>${rows.map((d) => `
            <tr>
              <td><span class="device-type">${icon(d.type === "Serial" ? "terminal" : "devices")} ${escapeHtml(d.type)}</span></td>
              <td><span class="repo">${escapeHtml(d.name)}</span>${d.error ? `<div class="digest" style="color:var(--danger)">${escapeHtml(d.error)}</div>` : ""}</td>
              <td class="mono">${escapeHtml(d.vidpid)}</td>
              <td><span class="badge ${d.status === "Attached" ? "success" : d.status === "Error" ? "danger" : "muted"}">${d.status}</span></td>
              <td>${d.machine !== "—" ? `<span class="badge accent">${escapeHtml(d.machine)}</span>` : '<span class="muted">—</span>'}</td>
              <td class="muted mono">${escapeHtml(d.bus)}:${escapeHtml(d.port)}</td>
              <td class="actions-cell visible">
                ${d.status === "Attached"
                  ? `<button class="btn sm" data-action="device-detach" data-id="${d.id}">${icon("x")} Desconectar</button>`
                  : d.status === "Error"
                    ? `<button class="btn sm" data-action="device-retry" data-id="${d.id}">${icon("refresh")} Tentar</button>`
                    : `<button class="btn sm" data-action="device-attach" data-id="${d.id}">${icon("plus")} Conectar</button>`}
              </td>
            </tr>`).join("") || `<tr><td colspan="7"><div class="empty"><b>Nenhum dispositivo</b>Conecte um USB ou serial.</div></td></tr>`}</tbody>
        </table></div>
      </section>`;
  }

  /* ------------------------------------------------------------------ desktop shell */
  function renderShell() {
    const now = new Date();
    const date = now.toLocaleDateString("pt-BR", { day: "2-digit", month: "short", year: "numeric" });
    const time = now.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
    $("#desktopBar").innerHTML = `
      <div class="shell-left"><span class="activities">Atividades</span><button class="shell-setup" data-action="setup-open" title="Configurar máquina">${icon("settings")} Configurar máquina <span class="setup-dot"></span></button></div>
      <div class="shell-center">${escapeHtml(date)} · ${escapeHtml(time)}</div>
      <div class="shell-right">${icon("wifi")}${icon("volume")}${icon("battery")}</div>`;
    $("#dock").innerHTML = `
      <button class="dock-icon" data-action="soon" data-label="Arquivos" title="Arquivos">${icon("folder")}</button>
      <button class="dock-icon" data-action="soon" data-label="Terminal" title="Terminal">${icon("terminal")}</button>
      <button class="dock-icon" data-action="soon" data-label="Navegador" title="Navegador">${icon("activity")}</button>
      <button class="dock-icon" data-action="soon" data-label="Settings" title="Settings">${icon("settings")}</button>
      <button class="dock-icon setup" data-action="setup-open" title="Configurar máquina">${icon("settings")}<span class="setup-badge">1</span></button>
      <button class="dock-icon app" data-action="window-restore" title="PodStack">${icon("containers")}<span class="running-dot"></span></button>`;
  }

  function renderTitlebar() {
    const maximized = $("#appWindow")?.classList.contains("maximized");
    $("#titlebar").innerHTML = `
      <div class="titlebar-left">
        <span class="titlebar-logo"></span>
        <span class="titlebar-title">PodStack <span class="titlebar-sep">—</span> ${escapeHtml(VIEWS[state.view])}</span>
      </div>
      <div class="titlebar-controls">
        <button class="win-btn" data-action="window-minimize" title="Minimizar">${icon("minimize")}</button>
        <button class="win-btn" data-action="window-maximize" title="${maximized ? "Restaurar" : "Maximizar"}">${icon(maximized ? "restore" : "maximize")}</button>
        <button class="win-btn close" data-action="window-close" title="Fechar">${icon("x")}</button>
      </div>`;
  }

  /* ------------------------------------------------------------------ setup / onboarding */
  function renderSetup() {
    const root = $("#setupRoot");
    if (!state.setup.open) {
      root.innerHTML = "";
      return;
    }
    const s = state.setup;
    const step = SETUP_STEPS[s.step];
    root.innerHTML = `
      <div class="setup-backdrop" data-action="setup-close"></div>
      <div class="setup-window" role="dialog" aria-modal="true" aria-label="PodStack Setup">
        <div class="setup-titlebar">
          <div class="setup-title"><span class="titlebar-logo"></span> PodStack Setup <span class="badge accent">primeira execução</span></div>
          <button class="win-btn" data-action="setup-close" title="Fechar">${icon("x")}</button>
        </div>
        <div class="setup-body">
          <aside class="setup-rail">
            ${SETUP_STEPS.map((item, i) => `
              <div class="setup-step ${i === s.step ? "active" : i < s.step ? "done" : ""}">
                <span class="setup-step-dot">${i < s.step ? icon("check") : i + 1}</span>
                <div><div class="setup-step-label">${item.label}</div><div class="setup-step-hint">${item.hint}</div></div>
              </div>`).join("")}
            <div class="setup-rail-foot">${icon("shield")}<span>Nada é alterado sem sua confirmação.</span></div>
          </aside>
          <section class="setup-main">
            <div class="setup-content">${setupStepContent()}</div>
            <div class="setup-footer">
              <span class="setup-footer-status">${s.busy ? "Aplicando alterações…" : step.hint}</span>
              <span class="grow"></span>
              ${s.step > 0 ? `<button class="btn ghost" data-action="setup-back" ${s.busy ? "disabled" : ""}>Voltar</button>` : ""}
              ${s.step === SETUP_STEPS.length - 1
                ? `<button class="btn primary" data-action="setup-finish">Abrir PodStack</button>`
                : `<button class="btn primary" data-action="setup-next" ${s.busy ? "disabled" : ""}>${s.step === 0 ? "Começar" : "Continuar"}</button>`}
            </div>
          </section>
        </div>
      </div>`;
    requestAnimationFrame(() => {
      const log = $("#setupLog");
      if (log) log.scrollTop = log.scrollHeight;
    });
  }

  function setupStepContent() {
    switch (state.setup.step) {
      case 0: return setupWelcome();
      case 1: return setupDiagnose();
      case 2: return setupDocker();
      case 3: return setupQemu();
      case 4: return setupComponents();
      case 5: return setupValidate();
      default: return setupDone();
    }
  }

  function setupCheckRow(label, status, detail) {
    const icons = { ok: "check", warn: "alert", missing: "x", optional: "check" };
    const badges = { ok: "success", warn: "warning", missing: "muted", optional: "muted" };
    const texts = { ok: "OK", warn: "atenção", missing: "ausente", optional: "opcional" };
    return `<div class="setup-check">
      <span class="setup-check-icon ${status}">${icon(icons[status] || "x")}</span>
      <div class="setup-check-text"><div class="setup-check-label">${label}</div><div class="setup-check-detail">${detail}</div></div>
      <span class="badge ${badges[status] || "muted"}">${texts[status] || status}</span>
    </div>`;
  }

  function setupInstallPanel() {
    const s = state.setup;
    if (!s.busy && s.logs.length === 0) return "";
    return `<div class="setup-progress">
      <div class="setup-progress-head"><span>${s.busy ? "Aplicando…" : "Concluído"}</span><span id="setupProgressText">${s.progress}%</span></div>
      <div class="bar"><div class="bar-fill" id="setupProgress" style="width:${s.progress}%"></div></div>
    </div>
    <div class="setup-log" id="setupLog">${s.logs.map((line) => `<div class="log-line"><span class="log-ts">$</span><span class="log-msg">${escapeHtml(line)}</span></div>`).join("")}</div>`;
  }

  function setupWelcome() {
    return `<div class="setup-hero">
      <div class="setup-hero-logo"><span class="logo"></span></div>
      <h1>Vamos preparar sua máquina</h1>
      <p class="setup-lead">O PodStack roda sobre o Podman nativo e usa QEMU/KVM para máquinas Linux. Vamos detectar o que já existe, remover conflitos e validar cada componente antes de abrir o app.</p>
      <div class="setup-feature-grid">
        <div>${icon("containers")}<b>Podman nativo</b><span>Engine rootless, socket e CLI, sem Docker.</span></div>
        <div>${icon("machines")}<b>QEMU/KVM</b><span>Máquinas Linux com cloud-init e virtiofs.</span></div>
        <div>${icon("networks")}<b>Domínios locais</b><span>Caddy para HTTPS em *.localhost.</span></div>
        <div>${icon("builds")}<b>Sem conflito</b><span>Docker é detectado e removido antes do Podman.</span></div>
      </div>
      <div class="setup-note">${icon("shield")}<span>Nada é alterado sem sua confirmação. Você pode pular etapas e revisar depois em Settings.</span></div>
    </div>`;
  }

  function setupDiagnose() {
    const s = state.setup;
    return `<h2 class="setup-h2">Diagnóstico do sistema</h2>
      <p class="setup-lead">Este é o estado atual da máquina. Docker é um conflito conhecido; Podman é obrigatório para o app.</p>
      <div class="setup-checks">
        ${setupCheckRow("Docker Engine", s.docker ? "warn" : "ok", s.docker ? "v27.3.1 · docker.socket ativo — será removido" : "não instalado")}
        ${setupCheckRow("Podman", !s.podman ? "missing" : s.podmanOutdated ? "warn" : "ok", !s.podman ? "ausente — instalação necessária" : s.podmanOutdated ? `v${s.podmanVersion} → ${s.podmanLatest} — atualização disponível` : `v${s.podmanVersion} · rootless · atualizado`)}
        ${setupCheckRow("QEMU/KVM", s.qemu ? "ok" : "missing", s.qemu ? "QEMU 9.0.2 · /dev/kvm disponível" : "ausente — necessário para machines")}
        ${setupCheckRow("/dev/kvm", s.kvm ? "ok" : "warn", s.kvm ? "disponível" : "indisponível — modo TCG (lento)")}
        ${setupCheckRow("virtiofsd", s.virtiofsd ? "ok" : "missing", s.virtiofsd ? "0.1.2" : "ausente — mounts de máquinas")}
        ${setupCheckRow("systemd (user)", "ok", "sessão de usuário ativa")}
        ${setupCheckRow("SQLite", s.sqlite ? "ok" : "missing", s.sqlite ? "3.51.2" : "ausente — histórico de builds")}
        ${setupCheckRow("Caddy", s.caddy ? "ok" : "optional", s.caddy ? "2.8.4" : "opcional — domínios locais")}
        ${setupCheckRow("k3s", s.k3s ? "ok" : "optional", s.k3s ? "v1.31" : "opcional — Kubernetes local")}
      </div>
      <div class="setup-scenario">
        <span>Cenário de demonstração</span>
        <select data-setup-scenario>
          <option value="missing" ${!s.podman ? "selected" : ""}>Podman ausente</option>
          <option value="outdated" ${s.podman && s.podmanOutdated ? "selected" : ""}>Podman 5.4.2 (atualizar)</option>
          <option value="current" ${s.podman && !s.podmanOutdated ? "selected" : ""}>Podman 5.8.9 (atualizado)</option>
        </select>
      </div>
      <div class="setup-actions">
        <button class="btn" data-action="setup-scan" ${s.scanning ? "disabled" : ""}>${icon("refresh")} ${s.scanning ? "Escaneando…" : "Escanear novamente"}</button>
      </div>`;
  }

  function setupDocker() {
    const s = state.setup;
    const podmanCurrent = s.podman && !s.podmanOutdated;
    const podmanOutdated = s.podman && s.podmanOutdated;
    const noAction = !s.docker && podmanCurrent;
    const label = s.docker
      ? (!s.podman ? "Desinstalar Docker e instalar Podman" : podmanOutdated ? "Desinstalar Docker e atualizar Podman" : "Desinstalar Docker")
      : (!s.podman ? "Instalar Podman" : podmanOutdated ? "Atualizar Podman" : "Verificar novamente");
    return `<h2 class="setup-h2">Docker → Podman</h2>
      <p class="setup-lead">O PodStack usa o Podman nativo. O Docker é desinstalado para evitar conflito de sockets, redes e containers; o Podman é instalado ou atualizado conforme a detecção.</p>
      ${s.docker
        ? `<div class="setup-card warn"><div class="setup-card-head">${icon("alert")}<div><b>Docker detectado</b><span>v27.3.1 · docker.socket ativo</span></div><span class="badge warning">conflito</span></div><p>Remover o Docker libera o socket, as redes e os containers para o Podman. Seus dados do Docker não são migrados automaticamente.</p></div>`
        : `<div class="setup-card success"><div class="setup-card-head">${icon("check")}<div><b>Docker não instalado</b><span>nenhum conflito de engine</span></div><span class="badge success">OK</span></div></div>`}
      <div class="setup-checks">
        ${setupCheckRow("Docker Engine", s.docker ? "warn" : "ok", s.docker ? "v27.3.1 · docker.socket ativo — será desinstalado" : "não instalado")}
        ${setupCheckRow("Podman", !s.podman ? "missing" : s.podmanOutdated ? "warn" : "ok", !s.podman ? "ausente — será instalado" : s.podmanOutdated ? `v${s.podmanVersion} → ${s.podmanLatest} — atualização disponível` : `v${s.podmanVersion} · rootless · atualizado`)}
      </div>
      ${!noAction
        ? `<div class="setup-card">
            <div class="setup-card-head">${icon("download")}<div><b>${s.docker ? "Substituir o engine" : "Preparar o Podman"}</b><span>${s.docker ? "Desinstala o Docker e instala/atualiza o Podman." : "Instala/atualiza o Podman e ativa o socket de usuário."}</span></div></div>
            <div class="setup-actions">
              <button class="btn primary" data-action="setup-apply-engine" ${s.busy ? "disabled" : ""}>${icon(s.docker ? "trash" : "download")} ${label}</button>
            </div>
          </div>`
        : `<div class="setup-card success"><div class="setup-card-head">${icon("check")}<div><b>Engine pronto</b><span>Docker ausente · Podman ${s.podmanVersion}</span></div><span class="badge success">OK</span></div></div>`}
      ${setupInstallPanel()}`;
  }

  function setupQemu() {
    const s = state.setup;
    return `<h2 class="setup-h2">QEMU/KVM</h2>
      <p class="setup-lead">Máquinas Linux rodam em QEMU/KVM. O virtiofsd é usado para mounts e o libvirt é opcional para integração com virsh.</p>
      <div class="setup-checks">
        ${setupCheckRow("QEMU", s.qemu ? "ok" : "missing", s.qemu ? "qemu-system 9.0.2" : "ausente")}
        ${setupCheckRow("/dev/kvm", s.kvm ? "ok" : "warn", s.kvm ? "disponível — aceleração KVM" : "indisponível — TCG será usado")}
        ${setupCheckRow("virtiofsd", s.virtiofsd ? "ok" : "missing", s.virtiofsd ? "instalado" : "ausente — mounts de máquinas")}
        ${setupCheckRow("libvirt", s.libvirt ? "ok" : "optional", s.libvirt ? "ativo" : "opcional — integração com virsh")}
      </div>
      ${!s.qemu ? `<div class="setup-card">
        <div class="setup-card-head">${icon("machines")}<div><b>Instalar QEMU/KVM</b><span>qemu-kvm · qemu-system · virtiofsd · edk2-ovmf</span></div></div>
        <div class="setup-actions"><button class="btn primary" data-action="setup-install-qemu" ${s.busy ? "disabled" : ""}>${icon("download")} Instalar virtualização</button></div>
      </div>` : `<div class="setup-card success"><div class="setup-card-head">${icon("check")}<div><b>Virtualização pronta</b><span>KVM ${s.kvm ? "ativo" : "em modo TCG"}</span></div><span class="badge success">OK</span></div></div>`}
      ${setupInstallPanel()}`;
  }

  function setupComponents() {
    const s = state.setup;
    return `<h2 class="setup-h2">Componentes do PodStack</h2>
      <p class="setup-lead">Obrigatórios: Podman socket, rootless e SQLite. Caddy habilita domínios locais; k3s é opcional.</p>
      <div class="setup-options">
        <label class="setup-option"><input type="checkbox" checked disabled /><div><b>Podman socket</b><span>systemd --user + socket 0600</span></div><span class="badge ${s.socket ? "success" : "muted"}">${s.socket ? "OK" : "pendente"}</span></label>
        <label class="setup-option"><input type="checkbox" checked disabled /><div><b>Rootless</b><span>sem root e sem setuid</span></div><span class="badge ${s.rootless ? "success" : "muted"}">${s.rootless ? "OK" : "pendente"}</span></label>
        <label class="setup-option"><input type="checkbox" checked disabled /><div><b>SQLite</b><span>histórico de builds</span></div><span class="badge ${s.sqlite ? "success" : "muted"}">${s.sqlite ? "OK" : "pendente"}</span></label>
        <label class="setup-option"><input type="checkbox" checked disabled /><div><b>Caddy</b><span>HTTPS para *.localhost</span></div><span class="badge ${s.caddy ? "success" : "muted"}">${s.caddy ? "OK" : "opcional"}</span></label>
        <label class="setup-option" data-action="setup-toggle-k3s"><input type="checkbox" ${s.installK3s ? "checked" : ""} /><div><b>k3s</b><span>Kubernetes local (opcional)</span></div><span class="badge ${s.k3s ? "success" : "muted"}">${s.k3s ? "OK" : "opcional"}</span></label>
      </div>
      <div class="setup-actions"><button class="btn primary" data-action="setup-install-components" ${s.busy ? "disabled" : ""}>${icon("download")} Instalar componentes</button></div>
      ${setupInstallPanel()}`;
  }

  function setupValidate() {
    const s = state.setup;
    const checks = [
      ["Podman", !s.podman ? "missing" : s.podmanOutdated ? "warn" : "ok", !s.podman ? "ausente" : s.podmanOutdated ? `v${s.podmanVersion} → ${s.podmanLatest}` : `v${s.podmanVersion} · atualizado`],
      ["Podman socket", s.socket ? "ok" : "missing", s.socket ? "ativo" : "inativo"],
      ["Rootless", s.rootless ? "ok" : "warn", s.rootless ? "configurado" : "pendente"],
      ["Docker ausente", !s.docker ? "ok" : "warn", !s.docker ? "removido" : "ainda instalado"],
      ["QEMU", s.qemu ? "ok" : "missing", s.qemu ? "instalado" : "ausente"],
      ["/dev/kvm", s.kvm ? "ok" : "warn", s.kvm ? "disponível" : "TCG (lento)"],
      ["virtiofsd", s.virtiofsd ? "ok" : "missing", s.virtiofsd ? "instalado" : "ausente"],
      ["SQLite", s.sqlite ? "ok" : "missing", s.sqlite ? "ok" : "ausente"],
      ["Caddy", s.caddy ? "ok" : "optional", s.caddy ? "ok" : "domínios desabilitados"],
      ["k3s", s.k3s ? "ok" : (s.installK3s ? "missing" : "optional"), s.k3s ? "ok" : (s.installK3s ? "não instalado" : "não selecionado")],
    ];
    const hasMissing = checks.some(([, status]) => status === "missing");
    return `<h2 class="setup-h2">Validação</h2>
      <p class="setup-lead">Confira o estado final. O app pode abrir com avisos, mas os itens obrigatórios precisam estar OK.</p>
      <div class="setup-checks">${checks.map(([label, status, detail]) => setupCheckRow(label, status, detail)).join("")}</div>
      ${s.validated ? `<div class="setup-card ${hasMissing ? "warn" : "success"}"><div class="setup-card-head">${icon(hasMissing ? "alert" : "check")}<div><b>${hasMissing ? "Alguns itens precisam de atenção" : "Tudo pronto para abrir o PodStack"}</b><span>${hasMissing ? "Você pode corrigir ou abrir mesmo assim." : "Todos os componentes obrigatórios estão OK."}</span></div></div></div>` : ""}
      <div class="setup-actions"><button class="btn" data-action="setup-validate" ${s.validating ? "disabled" : ""}>${icon("check")} ${s.validating ? "Validando…" : "Validar instalação"}</button></div>`;
  }

  function setupDone() {
    const s = state.setup;
    return `<div class="setup-done">
      <div class="setup-done-icon">${icon("check")}</div>
      <h1>Tudo pronto</h1>
      <p class="setup-lead">Sua máquina está configurada para rodar o PodStack.</p>
      <div class="setup-summary">
        <div><b>Podman</b><span>${s.podman ? `v${s.podmanVersion}${s.podmanOutdated ? " · atualização pendente" : " · rootless"}` : "pendente"}</span></div>
        <div><b>QEMU/KVM</b><span>${s.qemu ? (s.kvm ? "KVM ativo" : "TCG") : "pendente"}</span></div>
        <div><b>Socket</b><span>${s.socket ? "ativo" : "pendente"}</span></div>
        <div><b>Docker</b><span>${s.docker ? "remover pendente" : "removido"}</span></div>
        <div><b>Caddy</b><span>${s.caddy ? "ativo" : "opcional"}</span></div>
        <div><b>k3s</b><span>${s.k3s ? "ativo" : "opcional"}</span></div>
      </div>
      <div class="setup-actions"><button class="btn" data-action="setup-recheck">Rever diagnóstico</button></div>
    </div>`;
  }

  function runSetup(commands, onDone) {
    const s = state.setup;
    if (s.busy) return;
    s.busy = true;
    s.progress = 0;
    s.logs = [];
    renderSetup();
    let index = 0;
    const timer = setInterval(() => {
      if (!s.open) {
        clearInterval(timer);
        return;
      }
      s.logs.push(commands[index]);
      index++;
      s.progress = Math.round((index / commands.length) * 100);
      const log = $("#setupLog");
      if (log) {
        log.innerHTML = s.logs.map((line) => `<div class="log-line"><span class="log-ts">$</span><span class="log-msg">${escapeHtml(line)}</span></div>`).join("");
        log.scrollTop = log.scrollHeight;
      }
      const bar = $("#setupProgress");
      if (bar) bar.style.width = `${s.progress}%`;
      const text = $("#setupProgressText");
      if (text) text.textContent = `${s.progress}%`;
      if (index >= commands.length) {
        clearInterval(timer);
        setTimeout(() => {
          onDone();
          s.busy = false;
          s.logs.push("✔ concluído");
          renderSetup();
        }, 420);
      }
    }, 360);
  }

  /* ------------------------------------------------------------------ status */
  function renderStatusbar() {
    const running = runningCount();
    $("#statusbar").innerHTML = `
      <span class="stat"><span class="dot running"></span> podstackd conectado</span>
      <span class="sep"></span><span class="stat"><b>${running}</b>/${data.containers.length} containers</span>
      <span class="sep"></span><span class="stat"><b>${data.images.length}</b> imagens</span>
      <span class="sep"></span><span class="stat"><b>${data.volumes.length}</b> volumes</span>
      <span class="sep"></span><span class="stat"><b>${data.machines.length}</b> máquinas</span>
      <span class="sep"></span><span class="stat"><b>${data.devices.length}</b> devices</span>
      <span class="grow"></span>
      <span class="stat">rootless</span><span class="sep"></span><span class="stat">GPL-3.0-or-later</span>`;
  }

  /* ------------------------------------------------------------------ main */
  function renderMain() {
    const views = { containers: containersView, images: imagesView, volumes: volumesView, networks: networksView, compose: composeView, builds: buildsView, pods: podsView, machines: machinesView, devices: devicesView };
    $("#main").innerHTML = (views[state.view] || containersView)();
    startStats();
    requestAnimationFrame(() => {
      updateSpark();
      const terminal = $("#terminal");
      if (terminal) terminal.scrollTop = terminal.scrollHeight;
    });
  }

  function render() {
    renderSidebar();
    renderTopbar();
    renderMain();
    renderStatusbar();
    renderTitlebar();
  }

  /* ------------------------------------------------------------------ stats */
  function resetHistory() {
    state.cpuHistory = Array.from({ length: 30 }, () => Math.random() * 3 + 0.4);
    const base = parseFloat(selectedContainer()?.mem) || 64;
    state.memHistory = Array.from({ length: 30 }, () => Math.max(4, base + (Math.random() - 0.5) * base * 0.06));
    state.cpuHistoryFor = state.selectedContainer;
    state.memHistoryFor = state.selectedContainer;
  }

  function updateSpark() {
    const line = $("#sparkLine");
    const area = $("#sparkArea");
    if (!line || !area) return;
    if (state.cpuHistoryFor !== state.selectedContainer
        || state.memHistoryFor !== state.selectedContainer
        || state.cpuHistory.length === 0) {
      resetHistory();
    }
    const w = 300;
    const h = 64;
    const values = state.cpuHistory;
    const max = Math.max(5, ...values);
    const pts = values.map((v, i) => {
      const x = (i / (values.length - 1)) * w;
      const y = h - ((v / max) * (h - 8) + 4);
      return `${x.toFixed(2)},${y.toFixed(2)}`;
    });
    line.setAttribute("points", pts.join(" "));
    area.setAttribute("points", `0,${h} ${pts.join(" ")} ${w},${h}`);

    const memLine = $("#memSparkLine");
    const memArea = $("#memSparkArea");
    if (!memLine || !memArea) return;
    const memValues = state.memHistory;
    const memMin = Math.min(...memValues) * 0.94;
    const memMax = Math.max(...memValues) * 1.04;
    const memRange = Math.max(1, memMax - memMin);
    const memPts = memValues.map((v, i) => {
      const x = (i / (memValues.length - 1)) * w;
      const y = h - (((v - memMin) / memRange) * (h - 8) + 4);
      return `${x.toFixed(2)},${y.toFixed(2)}`;
    });
    memLine.setAttribute("points", memPts.join(" "));
    memArea.setAttribute("points", `0,${h} ${memPts.join(" ")} ${w},${h}`);
  }

  function startStats() {
    if (state.statsTimer) {
      clearInterval(state.statsTimer);
      state.statsTimer = null;
    }
    if (state.view !== "containers" || state.inspectorTab !== "stats") return;
    if (state.cpuHistoryFor !== state.selectedContainer
        || state.memHistoryFor !== state.selectedContainer) {
      resetHistory();
    }
    state.statsTimer = setInterval(() => {
      const c = selectedContainer();
      if (!c) return;
      const cpu = Math.random() * 3.4 + 0.4;
      state.cpuHistory.push(cpu);
      if (state.cpuHistory.length > 30) state.cpuHistory.shift();
      const cpuEl = $('[data-stat="cpu"]');
      if (cpuEl) cpuEl.textContent = `${cpu.toFixed(1)}%`;
      const nowEl = $('[data-stat="cpu-now"]');
      if (nowEl) nowEl.textContent = `${cpu.toFixed(1)}%`;

      const base = parseFloat(c.mem) || 64;
      const mem = Math.max(4, base + (Math.random() - 0.5) * base * 0.08);
      state.memHistory.push(mem);
      if (state.memHistory.length > 30) state.memHistory.shift();
      const memEl = $('[data-stat="mem"]');
      if (memEl) memEl.textContent = `${Math.round(mem)} MB`;
      const memNowEl = $('[data-stat="mem-now"]');
      if (memNowEl) memNowEl.textContent = `${Math.round(mem)} MB`;

      updateSpark();
    }, 2000);
  }

  /* ------------------------------------------------------------------ toast */
  function toast(message, kind = "info") {
    const el = document.createElement("div");
    el.className = `toast ${kind}`;
    el.innerHTML = `${icon(kind === "success" ? "check" : kind === "danger" ? "alert" : kind === "warning" ? "alert" : "activity")}<span>${escapeHtml(message)}</span>`;
    $("#toastStack").appendChild(el);
    setTimeout(() => {
      el.classList.add("leaving");
      setTimeout(() => el.remove(), 220);
    }, 3200);
  }

  function copyText(text) {
    if (navigator.clipboard && text) {
      navigator.clipboard.writeText(text).then(() => toast("Copiado para a área de transferência", "success")).catch(() => toast("Não foi possível copiar", "danger"));
    } else {
      toast("Copiado", "success");
    }
  }

  function setTheme(theme) {
    state.theme = theme;
    document.documentElement.dataset.theme = theme;
    render();
  }

  /* ------------------------------------------------------------------ modal */
  function openModal({ title, sub = "", body = "", confirmLabel = "Confirmar", danger = false, wide = false, onConfirm = null }) {
    state.modalConfirm = onConfirm;
    $("#modalRoot").innerHTML = `
      <div class="modal-backdrop">
        <div class="modal ${wide ? "wide" : ""}" role="dialog" aria-modal="true" aria-label="${escapeHtml(title)}">
          <div class="modal-head"><div class="modal-title">${escapeHtml(title)}</div>${sub ? `<div class="modal-sub">${escapeHtml(sub)}</div>` : ""}</div>
          <div class="modal-body">${body}</div>
          <div class="modal-foot">
            <button class="btn ghost" data-action="modal-close">Cancelar</button>
            ${onConfirm ? `<button class="btn ${danger ? "danger" : "primary"}" data-action="modal-confirm">${escapeHtml(confirmLabel)}</button>` : `<button class="btn primary" data-action="modal-close">Fechar</button>`}
          </div>
        </div>
      </div>`;
    const first = $("#modalRoot input, #modalRoot select, #modalRoot textarea");
    if (first) setTimeout(() => first.focus(), 30);
  }

  function closeModal() {
    $("#modalRoot").innerHTML = "";
    state.modalConfirm = null;
  }

  function confirmModal() {
    const fn = state.modalConfirm;
    closeModal();
    if (fn) fn();
  }

  function confirmAction(title, sub, onConfirm) {
    openModal({ title, sub, body: '<div class="hint">Esta ação não pode ser desfeita.</div>', confirmLabel: "Confirmar", danger: true, onConfirm });
  }

  function modalNewContainer() {
    openModal({
      title: "Novo container",
      sub: "Crie um container a partir de uma imagem.",
      confirmLabel: "Criar e iniciar",
      body: `
        <div class="form-grid">
          <div class="field full"><label>Imagem</label><input class="input" id="f-image" placeholder="nginx:alpine" /></div>
          <div class="field"><label>Nome</label><input class="input" id="f-name" placeholder="opcional" /></div>
          <div class="field"><label>Portas</label><input class="input" id="f-ports" placeholder="8080:80" /></div>
          <div class="field full"><label>Variáveis</label><input class="input" id="f-env" placeholder="A=B, C=D" /></div>
          <div class="field full"><label>Volumes</label><input class="input" id="f-mounts" placeholder="/host:/container" /></div>
        </div>
        <div class="hint">O container inicia imediatamente e aparece no projeto <code>default</code>.</div>`,
      onConfirm: () => {
        const image = ($("#f-image").value || "nginx:alpine").trim();
        const base = image.split("/").pop().split(":")[0].replace(/[^a-z0-9-]/gi, "-").toLowerCase();
        const name = ($("#f-name").value || `${base}-${Math.floor(Math.random() * 90 + 10)}`).trim();
        const ports = $("#f-ports").value.trim();
        const env = $("#f-env").value.split(",").map((s) => s.trim()).filter(Boolean);
        const mounts = $("#f-mounts").value.split(",").map((s) => s.trim()).filter(Boolean);
        data.containers.unshift({
          id: name, name, image, state: "running", status: "Up agora", health: "—",
          ports: ports || "—", project: "default", cpu: "0.0%", mem: "0 B",
          net: "0 B / 0 B", disk: "0 B / 0 MB", pids: 1, ip: "10.89.0.9", created: "agora",
          command: "—", env, mounts, files: DEFAULT_FILES,
          logs: [["agora", "ok", `container ${name} started`]],
        });
        state.selectedContainer = name;
        state.view = "containers";
        toast(`Container ${name} criado`, "success");
        render();
      },
    });
  }

  function modalPull() {
    openModal({
      title: "Pull de imagem",
      sub: "Baixe uma imagem de um registry.",
      confirmLabel: "Pull",
      body: `
        <div class="form-grid">
          <div class="field full"><label>Referência</label><input class="input" id="f-ref" placeholder="nginx:alpine" /></div>
          <div class="field"><label>Plataforma</label><input class="input" id="f-platform" placeholder="linux/amd64 (opcional)" /></div>
        </div>
        <div class="hint">O progresso aparece na barra de status e no topo da tela.</div>`,
      onConfirm: () => {
        const ref = ($("#f-ref").value || "alpine:3.20").trim();
        const slash = ref.lastIndexOf("/");
        const colon = ref.lastIndexOf(":");
        const repo = colon > slash ? ref.slice(0, colon) : ref;
        const tag = colon > slash ? ref.slice(colon + 1) : "latest";
        data.images.unshift({ repo, tag, digest: `sha256:${Math.random().toString(16).slice(2, 10)}…${Math.random().toString(16).slice(2, 6)}`, size: "—", created: "agora", used: 0 });
        toast(`Pull concluído: ${ref}`, "success");
        render();
      },
    });
  }

  function modalVolume() {
    openModal({
      title: "Novo volume",
      sub: "Persista dados fora do ciclo de vida do container.",
      confirmLabel: "Criar",
      body: `
        <div class="form-grid">
          <div class="field full"><label>Nome</label><input class="input" id="f-vname" placeholder="dados" /></div>
          <div class="field"><label>Driver</label><input class="input" id="f-vdriver" placeholder="local" /></div>
        </div>`,
      onConfirm: () => {
        const name = ($("#f-vname").value || `volume-${Math.floor(Math.random() * 90 + 10)}`).trim();
        data.volumes.unshift({ name, driver: ($("#f-vdriver").value || "local").trim(), size: "0 B", links: 0, mount: `/var/lib/containers/storage/volumes/${name}/_data` });
        toast(`Volume ${name} criado`, "success");
        render();
      },
    });
  }

  function modalNetwork() {
    openModal({
      title: "Nova rede",
      sub: "Isole containers em uma rede bridge.",
      confirmLabel: "Criar",
      body: `
        <div class="form-grid">
          <div class="field full"><label>Nome</label><input class="input" id="f-nname" placeholder="backend" /></div>
          <div class="field"><label>Subnet</label><input class="input" id="f-subnet" placeholder="10.90.0.0/24" /></div>
          <div class="field"><label>Gateway</label><input class="input" id="f-gateway" placeholder="10.90.0.1" /></div>
        </div>`,
      onConfirm: () => {
        const name = ($("#f-nname").value || `net-${Math.floor(Math.random() * 90 + 10)}`).trim();
        data.networks.unshift({ name, driver: "bridge", subnet: ($("#f-subnet").value || "10.90.0.0/24").trim(), containers: 0 });
        toast(`Rede ${name} criada`, "success");
        render();
      },
    });
  }

  function modalCompose(projectName = null) {
    const project = projectName ? data.compose.find((p) => p.name === projectName) : null;
    openModal({
      title: project ? `Subir ${project.name}` : "Up por diretório",
      sub: "O provider Compose executa up -d no diretório.",
      confirmLabel: "Up",
      body: `
        <div class="form-grid">
          <div class="field full"><label>Diretório do projeto</label><input class="input" id="f-dir" value="${escapeHtml(project ? project.dir : "")}" placeholder="/home/dev/projeto" /></div>
        </div>
        <div class="hint">Procura <code>compose.yaml</code> ou <code>docker-compose.yml</code>. O provider é detectado automaticamente (<code>docker-compose</code>, <code>podman compose</code>).</div>`,
      onConfirm: () => {
        const dir = ($("#f-dir").value || "").trim();
        if (!dir) { toast("Informe o diretório do projeto", "warning"); return; }
        if (project) {
          project.status = "running";
        } else {
          const name = dir.split("/").filter(Boolean).pop() || "projeto";
          data.compose.unshift({ name, dir, status: "running", services: ["web"], config: "compose.yaml" });
        }
        toast(`Projeto subiu: ${project ? project.name : dir}`, "success");
        render();
      },
    });
  }

  function modalBuild() {
    openModal({
      title: "Novo build",
      sub: "Empacota o contexto e envia para o Libpod.",
      confirmLabel: "Buildar",
      wide: true,
      body: `
        <div class="form-grid">
          <div class="field full"><label>Contexto</label><input class="input" id="f-context" placeholder="/home/dev/acme/api" /></div>
          <div class="field"><label>Dockerfile</label><input class="input" id="f-dockerfile" value="Dockerfile" /></div>
          <div class="field"><label>Tag</label><input class="input" id="f-tag" placeholder="ghcr.io/acme/api:latest" /></div>
          <div class="field"><label>Plataforma</label><input class="input" id="f-platform" placeholder="linux/amd64 (opcional)" /></div>
          <div class="field full"><label>Build args</label><input class="input" id="f-buildargs" placeholder="A=B, C=D" /></div>
        </div>
        <div class="hint">O histórico fica em SQLite; o log é gravado em <code>~/.local/state/podstack/builds/</code>.</div>`,
      onConfirm: () => {
        const context = ($("#f-context").value || "/home/dev/acme/api").trim();
        const tag = ($("#f-tag").value || "localhost/build:latest").trim();
        const b = { id: `b-${1043 + data.builds.length}`, image: tag, status: "running", duration: "—", started: "agora", trigger: "GUI", context, dockerfile: ($("#f-dockerfile").value || "Dockerfile").trim() };
        data.builds.unshift(b);
        state.selectedBuild = b.id;
        state.view = "builds";
        toast("Build iniciado", "info");
        render();
        setTimeout(() => {
          b.status = "success";
          b.duration = "18.4s";
          toast("Build concluído", "success");
          if (state.view === "builds") renderMain();
        }, 1800);
      },
    });
  }

  function modalSettings() {
    openModal({
      title: "Settings",
      sub: "Preferências do app e do engine.",
      confirmLabel: "Salvar",
      body: `
        <div class="form-grid">
          <div class="field"><label>Tema</label><select class="select" id="f-theme"><option value="dark" ${state.theme === "dark" ? "selected" : ""}>Escuro</option><option value="light" ${state.theme === "light" ? "selected" : ""}>Claro</option></select></div>
          <div class="field"><label>Modo do engine</label><select class="select" id="f-mode"><option>rootless</option><option>rootful</option></select></div>
        </div>
        <div class="hint">O modo rootful exige reiniciar o podstackd. Alterações de tema são imediatas.</div>`,
      onConfirm: () => {
        const theme = $("#f-theme").value;
        state.theme = theme;
        document.documentElement.dataset.theme = theme;
        toast("Configurações salvas", "success");
        render();
      },
    });
  }

  function modalTerminal(id) {
    const c = data.containers.find((x) => x.id === id);
    if (!c) return;
    openModal({
      title: `Terminal — ${c.name}`,
      sub: `${c.image} · /bin/sh`,
      wide: true,
      body: `<div class="terminal terminal-modal">${[
        ["root@api:/app#", "ls"],
        ["", "app  node_modules  package.json  package-lock.json  dist"],
        ["root@api:/app#", "node -v"],
        ["", "v20.18.1"],
        ["root@api:/app#", ""],
      ].map(([prompt, out]) => `<div class="log-line"><span class="log-ts">${prompt}</span><span class="log-msg">${escapeHtml(out)}</span></div>`).join("")}<div class="log-line"><span class="log-ts">root@api:/app#</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div></div>`,
    });
  }

  function modalComposeLogs(name) {
    const project = data.compose.find((p) => p.name === name);
    if (!project) return;
    const lines = [
      ["12:31:04", "info", `[${project.services[0] || "web"}] ready on :3000`],
      ["12:31:05", "info", `[${project.services[1] || "web"}] nginx started`],
      ["12:31:07", "info", "GET /api/projects 200 18ms"],
      ["12:31:12", "warn", "slow query (312ms)"],
    ];
    openModal({
      title: `Logs — ${project.name}`,
      sub: `agregado de ${project.services.length} serviços`,
      wide: true,
      body: `<div class="terminal terminal-modal">${lines.map(([ts, level, msg]) => logLine(ts, level, msg)).join("")}<div class="log-line"><span class="log-ts">${now()}</span><span class="log-level ok">$</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div></div>`,
    });
  }

  function modalNewMachine() {
    openModal({
      title: "Nova máquina",
      sub: "Cria uma VM Linux com QEMU/KVM e cloud-init.",
      confirmLabel: "Criar máquina",
      wide: true,
      body: `
        <div class="form-grid">
          <div class="field"><label>Nome</label><input class="input" id="f-mname" placeholder="dev" /></div>
          <div class="field"><label>Distro</label><select class="select" id="f-distro"><option>Ubuntu</option><option>Debian</option><option>Fedora</option></select></div>
          <div class="field"><label>Versão</label><input class="input" id="f-version" value="24.04" /></div>
          <div class="field"><label>Arch</label><select class="select" id="f-arch"><option>arm64</option><option>amd64</option></select></div>
          <div class="field"><label>CPUs</label><input class="input" id="f-cpus" type="number" value="4" min="1" /></div>
          <div class="field"><label>Memória (GB)</label><input class="input" id="f-mem" type="number" value="4" min="1" /></div>
          <div class="field"><label>Disco (GB)</label><input class="input" id="f-disk" type="number" value="20" min="1" /></div>
          <div class="field"><label>Usuário</label><input class="input" id="f-user" value="dev" /></div>
        </div>
        <div class="hint">A máquina aparece como <code>Provisioning</code> e fica <code>Running</code> ao final do cloud-init.</div>`,
      onConfirm: () => {
        const name = ($("#f-mname").value || `machine-${Math.floor(Math.random() * 90 + 10)}`).trim();
        const cpus = Number($("#f-cpus").value || 4);
        const mem = Number($("#f-mem").value || 4);
        const disk = Number($("#f-disk").value || 20);
        const machine = {
          id: name, name, distro: $("#f-distro").value, version: $("#f-version").value || "24.04",
          arch: $("#f-arch").value, cpus, memory: `${mem} GB`, disk: `${disk} GB`,
          status: "Provisioning", state: "provisioning", ip: "—", ssh: `ssh ${name}`,
          sshPort: 2225, user: $("#f-user").value || "dev", created: "agora", uptime: "—",
          kernel: "—", qemu: "QEMU 9.0.2", cpuPct: 0, memPct: 0, diskPct: 0,
          mounts: [], ports: ["2225→22"], logs: [["0.000000", "cloud-init: provisioning..."]],
        };
        data.machines.unshift(machine);
        state.selectedMachine = name;
        state.view = "machines";
        toast(`Máquina ${name} criada`, "success");
        render();
        setTimeout(() => {
          machine.status = "Running"; machine.state = "running";
          machine.ip = `192.168.215.${10 + data.machines.length}`;
          machine.uptime = "0m"; machine.kernel = "6.8.0-45-generic";
          toast(`Máquina ${name} pronta`, "success");
          if (state.view === "machines") renderMain();
        }, 1600);
      },
    });
  }

  function modalMachineTerminal(id) {
    const m = data.machines.find((x) => x.id === id);
    if (!m) return;
    openModal({
      title: `Terminal — ${m.name}`,
      sub: `${m.distro} ${m.version} · ${m.user}@${m.name}`,
      wide: true,
      body: `<div class="terminal terminal-modal">${[
        [`${m.user}@${m.name}:~$`, "uname -a"],
        ["", `Linux ${m.name} ${m.kernel} ... ${m.arch}`],
        [`${m.user}@${m.name}:~$`, "cloud-init status"],
        ["", "status: done"],
        [`${m.user}@${m.name}:~$`, ""],
      ].map(([prompt, out]) => `<div class="log-line"><span class="log-ts">${escapeHtml(prompt)}</span><span class="log-msg">${escapeHtml(out)}</span></div>`).join("")}<div class="log-line"><span class="log-ts">${escapeHtml(m.user)}@${escapeHtml(m.name)}:~$</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div></div>`,
    });
  }

  function modalMachineLogs(id) {
    const m = data.machines.find((x) => x.id === id);
    if (!m) return;
    openModal({
      title: `Boot log — ${m.name}`,
      sub: `${m.qemu} · ${m.kernel}`,
      wide: true,
      body: `<div class="terminal terminal-modal">${m.logs.map(([ts, msg]) => logLine(ts, "info", msg)).join("")}<div class="log-line"><span class="log-ts">${now()}</span><span class="log-level ok">$</span><span class="log-msg"> <span class="terminal-cursor"></span></span></div></div>`,
    });
  }

  function modalDeviceAttach(id) {
    const device = data.devices.find((x) => x.id === id);
    if (!device) return;
    const options = data.machines.map((m) => `<option value="${escapeHtml(m.name)}">${escapeHtml(m.name)} (${escapeHtml(m.distro)} ${escapeHtml(m.version)})</option>`).join("");
    openModal({
      title: `Conectar ${device.name}`,
      sub: `${device.type} · ${device.vidpid}`,
      confirmLabel: "Conectar",
      body: `<div class="form-grid"><div class="field full"><label>Máquina</label><select class="select" id="f-machine">${options}</select></div></div>
        <div class="hint">O dispositivo será removido do host e passado via QEMU <code>usb-host</code>. Desconecte antes de removê-lo fisicamente.</div>`,
      onConfirm: () => {
        device.status = "Attached";
        device.machine = $("#f-machine").value || "dev";
        device.error = null;
        toast(`${device.name} conectado a ${device.machine}`, "success");
        render();
      },
    });
  }

  function modalSoon(label) {
    toast(`${label} chega em um próximo marco.`, "info");
  }

  /* ------------------------------------------------------------------ actions */
  function handleAction(action, ds) {
    switch (action) {
      case "setup-open":
        closeModal();
        state.setup.open = true;
        state.setup.step = 0;
        state.setup.busy = false;
        state.setup.scanning = false;
        state.setup.validating = false;
        state.setup.logs = [];
        state.setup.progress = 0;
        renderSetup();
        break;
      case "setup-close":
        state.setup.open = false;
        state.setup.busy = false;
        state.setup.scanning = false;
        state.setup.validating = false;
        renderSetup();
        break;
      case "setup-next":
        if (state.setup.busy || state.setup.validating) break;
        state.setup.step = Math.min(SETUP_STEPS.length - 1, state.setup.step + 1);
        state.setup.logs = [];
        state.setup.progress = 0;
        renderSetup();
        break;
      case "setup-back":
        if (state.setup.busy) break;
        state.setup.step = Math.max(0, state.setup.step - 1);
        state.setup.logs = [];
        state.setup.progress = 0;
        renderSetup();
        break;
      case "setup-recheck":
        state.setup.step = 1;
        state.setup.logs = [];
        state.setup.progress = 0;
        renderSetup();
        break;
      case "setup-scan":
        if (state.setup.scanning) break;
        state.setup.scanning = true;
        renderSetup();
        setTimeout(() => {
          state.setup.scanning = false;
          toast("Diagnóstico atualizado", "success");
          renderSetup();
        }, 900);
        break;
      case "setup-toggle-k3s":
        state.setup.installK3s = !state.setup.installK3s;
        renderSetup();
        break;
      case "setup-apply-engine": {
        const s = state.setup;
        const hadDocker = s.docker;
        const wasMissing = !s.podman;
        const wasOutdated = s.podmanOutdated;
        const commands = [];
        if (s.docker) {
          commands.push("systemctl --user stop docker.socket docker.service");
          commands.push("sudo dnf remove -y docker-ce docker-ce-cli containerd.io");
        }
        if (!s.podman) {
          commands.push("sudo dnf install -y podman");
        } else if (s.podmanOutdated) {
          commands.push("sudo dnf upgrade -y podman");
        } else {
          commands.push("podman --version");
        }
        commands.push("systemctl --user enable --now podman.socket");
        commands.push("podman info --format '{{.Host.Security.Rootless}}'");
        runSetup(commands, () => {
          s.docker = false;
          s.podman = true;
          s.podmanVersion = s.podmanLatest;
          s.podmanOutdated = false;
          s.socket = true;
          s.rootless = true;
          toast(`${hadDocker ? "Docker desinstalado; " : ""}${wasMissing ? "Podman instalado" : wasOutdated ? "Podman atualizado" : "Podman OK"}`, "success");
        });
        break;
      }
      case "setup-install-qemu":
        runSetup([
          "sudo dnf install -y qemu-kvm qemu-system-x86_64 qemu-system-aarch64",
          "sudo dnf install -y virtiofsd libvirt-daemon-kvm edk2-ovmf",
          "sudo systemctl enable --now libvirtd",
          "test -e /dev/kvm && echo 'kvm: disponível'",
        ], () => {
          state.setup.qemu = true;
          state.setup.kvm = true;
          state.setup.virtiofsd = true;
          state.setup.libvirt = true;
          toast("QEMU/KVM instalado", "success");
        });
        break;
      case "setup-install-components": {
        const commands = [
          "sudo dnf install -y sqlite libarchive",
          "systemctl --user enable --now podman.socket",
          "podman system service --time=0 &",
          "sudo dnf install -y caddy",
        ];
        if (state.setup.installK3s) {
          commands.push("curl -sfL https://get.k3s.io | sh -s - --disable=traefik");
        }
        runSetup(commands, () => {
          state.setup.socket = true;
          state.setup.rootless = true;
          state.setup.sqlite = true;
          state.setup.caddy = true;
          if (state.setup.installK3s) state.setup.k3s = true;
          toast("Componentes instalados", "success");
        });
        break;
      }
      case "setup-validate":
        if (state.setup.validating) break;
        state.setup.validating = true;
        renderSetup();
        setTimeout(() => {
          state.setup.validating = false;
          state.setup.validated = true;
          toast("Validação concluída", "success");
          renderSetup();
        }, 1300);
        break;
      case "setup-finish":
        state.setup.open = false;
        state.setup.busy = false;
        state.setup.scanning = false;
        state.setup.validating = false;
        renderSetup();
        state.view = "containers";
        render();
        toast("Tudo pronto — bem-vindo ao PodStack", "success");
        break;
      case "theme":
        setTheme(state.theme === "dark" ? "light" : "dark");
        break;
      case "window-minimize":
        $("#appWindow").classList.add("minimized");
        toast("Janela minimizada — clique no ícone do PodStack na dock para restaurar.", "info");
        break;
      case "window-restore":
        $("#appWindow").classList.remove("minimized");
        toast("PodStack restaurado", "success");
        break;
      case "window-maximize": {
        const win = $("#appWindow");
        win.classList.toggle("maximized");
        renderTitlebar();
        toast(win.classList.contains("maximized") ? "Janela maximizada" : "Janela restaurada", "info");
        break;
      }
      case "window-close":
        toast("A demo permanece aberta para você explorar.", "info");
        break;
      case "settings":
        modalSettings();
        break;
      case "new-machine":
        modalNewMachine();
        break;
      case "machine-start": {
        const m = data.machines.find((x) => x.id === ds.id);
        if (m) {
          m.state = "running"; m.status = "Running";
          m.ip = `192.168.215.${10 + data.machines.indexOf(m)}`;
          m.uptime = "0m";
          toast(`Máquina ${m.name} iniciada`, "success");
          render();
        }
        break;
      }
      case "machine-stop": {
        const m = data.machines.find((x) => x.id === ds.id);
        if (m) { m.state = "stopped"; m.status = "Stopped"; m.ip = "—"; m.uptime = "—"; toast(`Máquina ${m.name} parada`, "warning"); render(); }
        break;
      }
      case "machine-restart": {
        const m = data.machines.find((x) => x.id === ds.id);
        if (m) { m.state = "running"; m.status = "Running"; m.uptime = "0m"; toast(`Máquina ${m.name} reiniciada`, "success"); render(); }
        break;
      }
      case "machine-delete": {
        const m = data.machines.find((x) => x.id === ds.id);
        if (!m) break;
        confirmAction(`Remover ${m.name}?`, "O disco da máquina e a chave SSH serão removidos. Dados perdidos.", () => {
          data.machines = data.machines.filter((x) => x !== m);
          state.selectedMachine = data.machines[0]?.id || null;
          toast(`Máquina ${m.name} removida`, "success");
          render();
        });
        break;
      }
      case "machine-ssh": {
        const m = data.machines.find((x) => x.id === ds.id);
        if (m) { copyText(m.ssh); toast(`SSH: ${m.ssh}`, "info"); }
        break;
      }
      case "machine-terminal":
        modalMachineTerminal(ds.id);
        break;
      case "machine-logs":
        modalMachineLogs(ds.id);
        break;
      case "device-attach":
        modalDeviceAttach(ds.id);
        break;
      case "device-detach": {
        const d = data.devices.find((x) => x.id === ds.id);
        if (!d) break;
        confirmAction(`Desconectar ${d.name}?`, `O dispositivo volta a ficar disponível no host (estava em ${d.machine}).`, () => {
          d.status = "Available"; d.machine = "—";
          toast(`${d.name} desconectado`, "success");
          render();
        });
        break;
      }
      case "device-retry": {
        const d = data.devices.find((x) => x.id === ds.id);
        if (d) { d.status = "Available"; d.machine = "—"; d.error = null; toast(`${d.name} liberado`, "success"); render(); }
        break;
      }
      case "device-scan":
        toast("Escaneando barramentos USB/serial…", "info");
        setTimeout(() => toast("Nenhum novo dispositivo encontrado", "info"), 900);
        break;
      case "copy":
        copyText(ds.value);
        break;
      case "soon":
        modalSoon(ds.label || "Recurso");
        break;
      case "refresh": {
        const btn = $('[data-action="refresh"]');
        if (btn) btn.classList.add("spinning");
        setTimeout(() => {
          if (btn) btn.classList.remove("spinning");
          toast("Atualizado", "success");
        }, 650);
        break;
      }
      case "new-container":
        modalNewContainer();
        break;
      case "start-all": {
        let n = 0;
        data.containers.forEach((c) => {
          if (c.state !== "running") {
            c.state = "running"; c.status = "Up agora"; c.health = "healthy"; n++;
          }
        });
        toast(n ? `${n} containers iniciados` : "Todos já estão rodando", n ? "success" : "info");
        render();
        break;
      }
      case "start": {
        const c = data.containers.find((x) => x.id === ds.id);
        if (c) { c.state = "running"; c.status = "Up agora"; c.health = "healthy"; toast(`${c.name} iniciado`, "success"); render(); }
        break;
      }
      case "stop": {
        const c = data.containers.find((x) => x.id === ds.id);
        if (c) { c.state = "stopped"; c.status = "Exited (0) agora"; c.health = "—"; toast(`${c.name} parado`, "warning"); render(); }
        break;
      }
      case "restart": {
        const c = data.containers.find((x) => x.id === ds.id);
        if (c) { c.state = "running"; c.status = "Up agora"; c.health = "healthy"; toast(`${c.name} reiniciado`, "success"); render(); }
        break;
      }
      case "remove": {
        const c = data.containers.find((x) => x.id === ds.id);
        if (!c) break;
        confirmAction(`Remover ${c.name}?`, "O container e suas alterações efêmeras serão removidos.", () => {
          data.containers = data.containers.filter((x) => x.id !== c.id);
          if (state.selectedContainer === c.id) state.selectedContainer = data.containers[0]?.id || null;
          toast(`${c.name} removido`, "success");
          render();
        });
        break;
      }
      case "terminal":
        modalTerminal(ds.id);
        break;
      case "clear-logs": {
        const c = selectedContainer();
        if (c) c.logs = [];
        renderMain();
        toast("Logs limpos", "info");
        break;
      }
      case "upload":
        toast("Selecione um arquivo no diálogo nativo", "info");
        break;
      case "file":
        toast(`Download de ${ds.name} iniciado`, "info");
        break;
      case "pull":
        modalPull();
        break;
      case "prune-images":
        confirmAction("Limpar imagens?", "Remove imagens dangling e sem containers em uso.", () => {
          data.images = data.images.filter((i) => i.used > 0);
          toast("Imagens removidas", "success");
          render();
        });
        break;
      case "remove-image": {
        const [repo, tag] = (ds.id || "").split(":");
        const image = data.images.find((i) => i.repo === repo && i.tag === tag);
        if (!image) break;
        confirmAction(`Remover ${repo}:${tag}?`, "A imagem será removida permanentemente.", () => {
          data.images = data.images.filter((i) => i !== image);
          toast("Imagem removida", "success");
          render();
        });
        break;
      }
      case "create-volume":
        modalVolume();
        break;
      case "prune-volumes":
        confirmAction("Limpar volumes?", "Remove volumes sem containers usando.", () => {
          data.volumes = data.volumes.filter((v) => v.links > 0);
          toast("Volumes removidos", "success");
          render();
        });
        break;
      case "remove-volume": {
        const v = data.volumes.find((x) => x.name === ds.id);
        if (!v) break;
        confirmAction(`Remover ${v.name}?`, "O volume e seus dados serão removidos.", () => {
          data.volumes = data.volumes.filter((x) => x !== v);
          toast("Volume removido", "success");
          render();
        });
        break;
      }
      case "create-network":
        modalNetwork();
        break;
      case "remove-network": {
        const n = data.networks.find((x) => x.name === ds.id);
        if (!n) break;
        confirmAction(`Remover ${n.name}?`, "Containers conectados bloqueiam a remoção.", () => {
          data.networks = data.networks.filter((x) => x !== n);
          toast("Rede removida", "success");
          render();
        });
        break;
      }
      case "compose-up":
        if (ds.id) {
          const p = data.compose.find((x) => x.name === ds.id);
          if (p) { p.status = "running"; toast(`Projeto ${p.name} no ar`, "success"); render(); }
        } else {
          modalCompose();
        }
        break;
      case "compose-down": {
        const p = data.compose.find((x) => x.name === ds.id);
        if (!p) break;
        confirmAction(`Derrubar ${p.name}?`, "Containers e rede do projeto serão removidos; volumes são preservados.", () => {
          p.status = "stopped";
          toast(`Projeto ${p.name} derrubado`, "warning");
          render();
        });
        break;
      }
      case "compose-restart": {
        const p = data.compose.find((x) => x.name === ds.id);
        if (p) { p.status = "running"; toast(`Projeto ${p.name} reiniciado`, "success"); render(); }
        break;
      }
      case "compose-logs":
        modalComposeLogs(ds.id);
        break;
      case "new-build":
        modalBuild();
        break;
      case "select-build":
        state.selectedBuild = ds.id;
        renderMain();
        break;
      case "remove-build": {
        const b = data.builds.find((x) => x.id === ds.id);
        if (!b) break;
        confirmAction(`Remover ${b.id}?`, "Apenas o registro e o log são removidos; a imagem permanece.", () => {
          data.builds = data.builds.filter((x) => x !== b);
          state.selectedBuild = data.builds[0]?.id || null;
          toast("Registro removido", "success");
          render();
        });
        break;
      }
      case "modal-confirm":
        confirmModal();
        break;
      case "modal-close":
        closeModal();
        break;
      default:
        break;
    }
  }

  /* ------------------------------------------------------------------ events */
  document.addEventListener("click", (event) => {
    const actionEl = event.target.closest("[data-action]");
    if (actionEl) {
      event.preventDefault();
      handleAction(actionEl.dataset.action, actionEl.dataset);
      return;
    }
    const tab = event.target.closest("[data-tab]");
    if (tab) {
      state.inspectorTab = tab.dataset.tab;
      renderMain();
      return;
    }
    const filter = event.target.closest("[data-filter]");
    if (filter) {
      state.containerFilter = filter.dataset.filter;
      renderMain();
      return;
    }
    const container = event.target.closest("[data-container]");
    if (container) {
      state.selectedContainer = container.dataset.container;
      if (state.inspectorTab === "stats") resetHistory();
      renderMain();
      return;
    }
    const machine = event.target.closest("[data-machine]");
    if (machine) {
      state.selectedMachine = machine.dataset.machine;
      renderMain();
      return;
    }
    const view = event.target.closest("[data-view]");
    if (view) {
      state.view = view.dataset.view;
      state.search = "";
      state.selectedContainer = data.containers[0]?.id || null;
      render();
      return;
    }
    if (event.target.classList && event.target.classList.contains("modal-backdrop")) {
      closeModal();
    }
  });

  document.addEventListener("change", (event) => {
    const select = event.target.closest("[data-setup-scenario]");
    if (!select) return;
    const s = state.setup;
    const value = select.value;
    s.podman = value !== "missing";
    s.podmanOutdated = value === "outdated";
    s.podmanVersion = value === "current" ? s.podmanLatest : value === "outdated" ? "5.4.2" : "";
    if (!s.podman) s.podmanVersion = "";
    renderSetup();
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (state.setup.open) {
        state.setup.open = false;
        state.setup.busy = false;
        renderSetup();
      } else {
        closeModal();
      }
    }
    if (event.key === "/" && !event.target.matches("input, textarea, select")) {
      event.preventDefault();
      $("#search")?.focus();
    }
  });

  /* ------------------------------------------------------------------ init */
  renderShell();
  resetHistory();
  render();
})();
