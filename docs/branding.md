# Branding e assets — PodStack

> Identidade visual própria, alinhada ao princípio 7 da [constituição](constitution.md): sem copiar a linguagem visual do OrbStack (esferas/órbitas), a baleia do Docker ou o roxo/lilás do Podman.
>
> **Status (2026-11-08):** direção **aprovada** — **Cápsula** como símbolo principal e **Camadas** como símbolo secundário. O **Monograma P** não foi aprovado e não deve ser usado.
>
> **Licença:** todo o projeto (código, documentação e assets) é **GPL-3.0-or-later**.

---

## 1. Arquivos

| Arquivo | Conteúdo | Status |
|---|---|---|
| [`../assets/podstack-capsule.svg`](../assets/podstack-capsule.svg) | Símbolo principal (cápsula com blocos internos) | **aprovado** |
| [`../assets/podstack-layers.svg`](../assets/podstack-layers.svg) | Símbolo secundário (três camadas empilhadas) | **aprovado** |
| [`../assets/podstack-capsule-small.svg`](../assets/podstack-capsule-small.svg) | Cápsula simplificada para 16–24 px | **aprovado** |
| [`../assets/podstack-icon.svg`](../assets/podstack-icon.svg) | Ícone do app (256×256, fundo escuro) | **aprovado** |
| [`../assets/podstack-logo.svg`](../assets/podstack-logo.svg) | Lockup horizontal (cápsula + wordmark) | **aprovado** |
| [`../assets/png/podstack-icon-{16,24,32,48,64,128,256,512}.png`](../assets/png/) | Ícones para `hicolor`/launcher | **gerado** |
| [`../assets/png/tray/podstack-tray-{16,22,24,32,48,64}.png`](../assets/png/tray/) | Bandeja (SNI) em tamanhos pequenos/médios, a partir de `podstack-capsule-tray.svg` | **gerado** |
| [`../assets/png/tray-validation.png`](../assets/png/tray-validation.png) | Folha de validação (escuro/claro) | **evidência** |
| [`../assets/favicon.ico`](../assets/favicon.ico) | Favicon multi-size (16/32/48) | **gerado** |
| [`../assets/favicon-32.png`](../assets/favicon-32.png) | Favicon PNG 32×32 | **gerado** |
| [`../assets/og-image.svg`](../assets/og-image.svg) / [`og-image.png`](../assets/og-image.png) | Open Graph 1200×630 | **gerado** |
| [`../assets/podstack_logo_concepts.svg`](../assets/podstack_logo_concepts.svg) | Folha histórica com os três conceitos | referência |
| [`../assets/logo.md`](../assets/logo.md) | Racional original dos conceitos | referência |

Todos os SVGs novos são **originais**, derivados dos conceitos aprovados, sem metadata C2PA e sem elementos de marca de terceiros.

---

## 2. Conceitos e decisão

| # | Conceito | Ideia | Decisão |
|---|---|---|---|
| 1 | **Camadas** | Pilha de três camadas com barra ciano no topo | **Aprovado como secundário** |
| 2 | **Cápsula** | Cápsula índigo com blocos internos (serviços de um Compose) | **Aprovado como principal** |
| 3 | **Monograma P** | "P" desenhado com barras empilhadas na base | **Não aprovado** |

**Direção:** usar a **cápsula** em README, onboarding, ícone do app e sidebar; usar **camadas** em materiais secundários (site, apresentações, splash alternativo). Para 16–24 px (bandeja/favicon), usar a **cápsula compacta** — os blocos internos da cápsula completa somem nesse tamanho.

---

## 3. Paleta

| Token | Hex | Uso |
|---|---|---|
| `brand.indigo.700` | `#3730A3` | Cor-base do símbolo, fundos escuros de marca |
| `brand.indigo.500` | `#6366F1` | Ação primária, links, foco (UI dark) |
| `brand.indigo.200` | `#A5B4FC` | Detalhes, estados hover suaves |
| `brand.cyan.400` | `#22D3EE` | Acento de "ativo/rodando", destaque |
| `brand.cyan.900` | `#0E3A47` | Detalhe escuro dentro do ciano (contraste) |

**Status (UI):** sucesso `#3FB950`, atenção `#D29922`, erro `#F85149`, info `#58A6FF`.
**Neutros (dark):** base `#0F1115`, superfície `#161A21`, elevado `#1D232C`, borda `#2A313C`, texto `#E6EAF0`, secundário `#9AA4B2`.

Contraste: texto branco sobre `#3730A3` atende AA para texto grande; para texto pequeno use `#E6EAF0`/`#FFFFFF` e valide com ferramenta de contraste. Nunca use ciano `#22D3EE` como cor de texto sobre branco.

---

## 4. Tipografia

- **UI/wordmark:** Inter, Manrope ou IBM Plex Sans, peso médio (500/600).
- **Wordmark:** "PodStack" com "Pod" em `#E6EAF0` e "Stack" em `#22D3EE` (lockup aprovado); sobre fundo claro, usar "Pod" em `#1F2328`.
- **CLI/docs:** monoespaçada (JetBrains Mono / IBM Plex Mono) para `podstack`, comandos e blocos de código.
- **Não** usar a fonte do OrbStack nem imitar o wordmark dele.

---

## 5. Uso do logo

### 5.1 Versões

| Versão | Arquivo | Onde |
|---|---|---|
| **Cápsula + wordmark** | `podstack-logo.svg` | README, site, onboarding, splash |
| **Cápsula isolada** | `podstack-capsule.svg` | ícone do app, sidebar, materiais |
| **Cápsula compacta** | `podstack-capsule-small.svg` | favicon |
| **Cápsula da bandeja** | `podstack-capsule-tray.svg` | bandeja (16–64 px), corpo mais alto para preencher o slot |
| **Camadas** | `podstack-layers.svg` | site, apresentações, splash alternativo |
| **Ícone do app** | `podstack-icon.svg` | launcher, `.desktop`, PNGs 16–512 |

### 5.2 Área de respiro e tamanho mínimo

- Respiro mínimo: altura da cápsula × 0,25 em todos os lados.
- Tamanho mínimo: **16 px** (cápsula compacta) / **24 px** (cápsula completa).
- Wordmark mínimo: 96 px de largura.
- Em 16 px, usar a cápsula compacta; não tentar renderizar os três blocos internos.

### 5.3 Fundos

- Preferir fundo escuro (`#0F1115`) ou claro (`#F6F8FA`).
- Sobre foto/imagem, usar a versão monocromática branca com sombra sutil.
- Não colocar o logo sobre ciano/índigo saturado sem contraste validado.

### 5.4 Não fazer

- Não usar o **monograma P** (não aprovado).
- Não usar esferas, órbitas, anéis ou gradientes roxos (território do OrbStack/Podman).
- Não usar a baleia do Docker nem elementos de marca de terceiros.
- Não distorcer, rotacionar, aplicar contorno grosso ou sombra pesada.
- Não recolorir fora da paleta sem aprovação.
- Não usar o logo para sugerir afiliação com a Orbital Labs.

---

## 6. Aplicação no produto

| Contexto | Implementação |
|---|---|
| Ícone do app | `podstack-icon.svg` + PNGs 16–512 em `hicolor` |
| Bandeja (SNI) | PNGs `podstack-tray-{16,22,24,32,48,64}.png`, gerados de `podstack-capsule-tray.svg`; badge de status (atenção `#D29922` / erro `#F85149`) desenhado em runtime sobre o ícone base |
| Sidebar | cápsula 20 px + wordmark opcional no topo |
| Onboarding | cápsula + wordmark, fundo escuro |
| CLI | cápsula compacta em `--help`/`version` (sem arte pesada) |
| Docs | lockup no topo do README; cápsula compacta em favicon |
| Website | cápsula + wordmark; camadas como elemento secundário; paleta índigo/ciano |

Tokens de UI em [`ui-map.md`](ui-map.md).

---

## 7. Governança

- Alterações de logo/paleta exigem aprovação de Produto + Design e atualização deste documento.
- Assets versionados em `assets/` com nome estável:
  - `podstack-logo.svg` (cápsula + wordmark)
  - `podstack-capsule.svg` (símbolo principal)
  - `podstack-layers.svg` (símbolo secundário)
  - `podstack-capsule-small.svg` (16–24 px)
  - `podstack-icon.svg` (ícone do app)
  - `podstack_logo_concepts.svg` (histórico)
- Manter os conceitos para histórico.
- Licença dos assets: **GPL-3.0-or-later** (mesma do projeto); não usar marcas de terceiros.

---

## 8. Pendências

- [x] Aprovar a direção (Cápsula principal + Camadas secundário).
- [x] Gerar SVGs utilizáveis (cápsula, camadas, compacta, ícone, lockup).
- [x] Gerar PNGs do ícone (16–512) para `hicolor` e da bandeja (16/22/24/32/48/64).
- [x] Validar legibilidade de 16–64 px em fundos escuro (`#0F1115`) e claro (`#F6F8FA`) — ver [`png/tray-validation.png`](../assets/png/tray-validation.png).
- [x] Criar favicon (`.ico` 16/32/48 + PNG 32) e imagem Open Graph 1200×630.
- [ ] Validar no tray real (Noctalia/KDE) quando a GUI existir (M7).
- [x] Definir licença dos assets junto com a licença do projeto: **GPL-3.0-or-later**.

### Evidência de validação

- Ícones de bandeja em 16, 22, 24, 32, 48 e 64 px sobre `#0F1115` e `#F6F8FA`: cápsula e blocos legíveis em todos os tamanhos.
- O ícone de 16 px usa a cápsula compacta (dois blocos), sem perda de leitura.
- Favicon `.ico` contém os frames 16/32/48; Open Graph gerado em 1200×630.
- A validação no tray real depende do app (SNI) e fica para o M7; o asset está pronto.
