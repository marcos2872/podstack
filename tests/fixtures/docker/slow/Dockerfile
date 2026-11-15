# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build de ~30 s, para testar cancelamento (#74), progresso e o log em
# streaming. Cada RUN sleep aparece como um passo no log.
#
#   podstack build tests/fixtures/docker/slow -t local/fixture-slow:1
#
# Na GUI: inicie o build e clique em Cancelar durante o primeiro sleep.

FROM alpine:3.20

RUN echo "passo 1: preparando (10s)" && sleep 10
RUN echo "passo 2: compilando (10s)" && sleep 10
RUN echo "passo 3: empacotando (10s)" && sleep 10 && echo "concluido"

CMD ["sh", "-c", "echo fixture-slow ok && sleep 300"]
