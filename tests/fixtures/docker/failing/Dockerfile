# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build que falha de propósito, para testar a superfície de erro: o log deve
# mostrar a linha de erro e o status não pode ficar "em andamento" (#77, #74).
#
#   podstack build tests/fixtures/docker/failing -t local/fixture-failing:1

FROM alpine:3.20

RUN echo "erro proposital: arquivo de configuração ausente" >&2 && exit 1
