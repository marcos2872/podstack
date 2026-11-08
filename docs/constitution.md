# Constituição do PodStack

> Documento fundador do projeto (SDD). Toda spec, plano, tarefa e PR deve respeitar estes princípios.
> Versão: 1.0 · Status: ratificado · Alterações exigem ADR e revisão explícita.

## 1. A especificação é a fonte de verdade

- Nenhuma funcionalidade começa sem requisito identificado (`FR-*`/`NFR-*`) e critério de aceite.
- Divergência entre código e spec é bug — corrige-se o código ou se atualiza a spec, nunca se ignora.
- Decisões técnicas relevantes viram ADR em `docs/adr/`.
- Documentação e código vivem no mesmo repositório e evoluem juntos.

## 2. Clean-room e identidade própria

- O PodStack é inspirado em fluxos e comportamento (não protegidos), mas é escrito do zero.
- **Proibido** reutilizar assets, ícones, sons, fontes, textos longos, layout pixel-a-pixel ou binários do OrbStack.
- **Proibido** copiar código proprietário, fazer engenharia reversa de algoritmos protegidos ou contornar licenças/DRM.
- "OrbStack" é marca registrada; o produto, namespace, domínio e pacote usam **PodStack**.
- O baseline funcional é documentado em `docs/reference/` apenas como especificação de comportamento.

## 3. Linux nativo, Podman primeiro

- Containers rodam no **Podman nativo**, sem VM intermediária. Não reimplementamos o engine.
- Rootless é o modo padrão; rootful é opt-in explícito com aviso de risco.
- Nenhuma dependência obrigatória de Docker Desktop, Docker Engine ou libvirt.
- VMs existem apenas para o recurso "Machines" e para o cluster Kubernetes gerenciado.

## 4. Menor privilégio por padrão

- GUI, CLI e daemon rodam como o usuário. Nada de setuid no app.
- Operações privilegiadas passam por um helper mínimo, systemd system + polkit, com API declarativa e auditável.
- O socket de controle é `0700`/`0600`, valida `SO_PEERCRED` e nunca escuta em TCP.
- O helper nunca executa comandos arbitrários; apenas operações nomeadas com parâmetros validados.
- Segredos (registry, SSH, CA) ficam em arquivos `0600` ou no keyring do desktop; nunca em logs.

## 5. Qualidade verificável

- Todo PR precisa de teste automatizado ou justificativa explícita.
- Pirâmide: unitários (Qt Test) → integração (Podman real) → E2E (QML/CLI) → performance.
- `clang-format`, `clang-tidy`, build sem warnings novos no CI.
- Nenhum marco é declarado concluído sem evidência de teste/lint anexada.

## 6. Desempenho e responsividade

- Startup da GUI: **< 1 s** em hardware de referência (SSD, 4 cores).
- Memória em repouso: **< 180 MB RSS** (GUI + daemon somados).
- CPU em repouso: **< 1%** de um core; eventos via stream, sem polling agressivo.
- Nenhuma operação de rede/IO no thread da UI; feedback visual em < 100 ms.
- Listas com 1.000+ itens devem rolar a 60 fps (modelos virtualizados).

## 7. Compatibilidade

- Alvo primário: **Fedora 43+** (Podman ≥ 5.8, Qt ≥ 6.10, Wayland e X11).
- Funciona em GNOME, KDE Plasma, niri/Noctalia e wlroots; não depender de APIs exclusivas de um DE.
- Empacotamento primário em RPM; AppImage para demo; Flatpak não previsto no v1 (reavaliar sob demanda — ADR-0006).
- Interfaces com Podman devem tolerar 5.8+ e 6.x; versão da API é descoberta em runtime.

## 8. Acessibilidade e internacionalização

- Navegação completa por teclado; foco visível; labels acessíveis em todos os controles.
- Contraste mínimo WCAG AA; respeitar tema claro/escuro e escala de fonte do sistema.
- Toda string de UI passa por tradução (`qsTr`); pt-BR e en-US no v1.
- Nada de texto embutido em imagem; ícones com nome acessível.

## 9. Observabilidade e suporte

- Logs estruturados (JSON) em `~/.local/state/podstack/logs/`, com rotação e redaction.
- Diagnóstico (`podstack doctor`, `podstack report`) sem segredos por padrão.
- Telemetria é **opt-in por categoria** e desligada por padrão.
- Erros exibidos ao usuário têm causa, ação sugerida e código estável.

## 10. Evolução

- Licença do projeto: **GPL-3.0-or-later**. Toda dependência deve ser compatível; arquivos fonte levam `SPDX-License-Identifier`.
- API do daemon versionada (`api: 1`), com compatibilidade para ao menos uma versão anterior.
- Migrações de dados são idempotentes e reversíveis; backup antes de migrar.
- Features experimentais ficam atrás de flag e documentadas como tal.
- O roadmap prioriza paridade funcional, estabilidade e segurança — nessa ordem.
