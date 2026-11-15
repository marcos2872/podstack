# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build rápido e feliz: valida "Buildar" concluindo, histórico, log e o
# seletor de plataforma (uma plataforma).
#
#   podstack build tests/fixtures/docker/simple -t local/fixture-simple:1
#
# Na GUI: Builds → Novo build → contexto = este diretório, tag local/fixture-simple:1.

FROM alpine:3.20

RUN adduser -D -u 1000 fixture
USER fixture
WORKDIR /home/fixture

CMD ["sh", "-c", "echo fixture-simple ok && sleep 300"]
