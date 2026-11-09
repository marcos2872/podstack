Li o PRD do PodStack. Com base nele, aqui vão três direções de logo.

Esses três conceitos partem do nome (Pod + Stack) e do que o PRD pede: containers, VMs, Compose e Kubernetes em um só app.

**O que a logo precisa comunicar**
- **Camadas e integração:** o PRD vende "uma única experiência" para containers, VMs, K8s e domínios locais, então uma pilha ou um agrupamento funciona bem.
- **Velocidade e leveza:** as metas são startup abaixo de 1 s e uso baixo de memória. Formas simples e geometria limpa passam essa ideia.
- **Identidade própria:** o princípio 7 do PRD pede paleta e ícones originais, sem copiar o OrbStack. Por isso evitei esferas e órbitas, que são o território visual dele, e também a baleia do Docker e a paleta roxa/lilás típica do Podman.
- **Funcionar em tamanho pequeno:** o ícone vai aparecer na bandeja do sistema (StatusNotifierItem), na sidebar e no launcher do Fedora. Ele tem que ser legível com 16–64 px, em modo escuro e claro.

**Como eu avaliaria cada direção**
1. **Camadas:** é a mais literal, e a barra ciano no topo dá um ponto de destaque. É clara e fácil de reduzir, mas pode parecer genérica, porque pilhas são comuns em produtos de infraestrutura.
2. **Cápsula:** lembra um pod (o nome vem daí) e os blocos internos representam os serviços de um projeto Compose. É a mais ligada ao produto e a mais memorável. Em 16 px os blocos internos podem se perder, então talvez precise de uma versão simplificada para a bandeja.
3. **Monograma P:** funciona bem como ícone de app e no launcher, e as barras empilhadas na base reforçam o "stack". É a mais fácil de aplicar em favicon e CLI. Como é um símbolo baseado em letra, é menos distintivo conceitualmente.

Minha recomendação é a **cápsula (2)** como símbolo principal, com o **monograma P (3)** como versão compacta para a bandeja e tamanhos pequenos.

**Cores:** índigo profundo (`#3730A3`) como base e ciano (`#22D3EE`) como acento de "ativo/rodando". Combina com o dark-first do PRD e destaca estados como container em execução. Evite tons de roxo muito próximos do Podman.

**Tipografia do wordmark:** uma sans geométrica ou humanista de peso médio (Inter, Manrope ou IBM Plex Sans), em "PodStack" com ou sem destaque no "Stack". Como o produto também é CLI, uma variante em monoespaçada (`podstack`) pode servir para a documentação.

Quer que eu aprofunde uma das direções, por exemplo a cápsula com versões em 16/32/256 px, em modo claro e escuro e junto do wordmark? Posso também gerar o SVG final.
