# Nord LV2 1.0.0

> Uma releitura dos timbres do Nord, usando o Nord Stage 4 como inspiração.

Instrumento LV2 para Linux, destinado ao Zynthian Oram em Raspberry Pi 5
(aarch64). É um projeto independente, sem vínculo oficial com a Nord.

- **NPNO:** reprodução dos arquivos de piano `.npno`.
- **NSMP4:** reprodução dos instrumentos sampleados `.nsmp4`.
- **Pure:** síntese interna com dois osciladores, filtros, envelopes, LFOs,
  matriz de modulação e Drive.

Um único plugin: nome **Nord**, URI **`urn:nord:instrument`**, categoria Zynthian
**Workstation**. O estado usa `instrument_id`; símbolos/índices dos controles e
identidades dos instrumentos existentes são preservados.

## Compilação

Requisitos: Linux, CMake >= 3.24, compilador C++20, Python 3 e desenvolvimento
Cairo/X11. No Debian Bookworm/Oram, por exemplo:

```sh
sudo apt-get install build-essential cmake pkg-config python3 ca-certificates libcairo2-dev libx11-dev libxext-dev
# Validação contínua (CTest e bundle):
sudo apt-get install binutils python3-numpy liblilv-0-0 python3-rdflib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

O CMake lê `dependencies.lock.json` e usa FetchContent para baixar automaticamente
DPF, Pugl, SST e SIMDe na primeira configuração, por commits imutáveis e arquivos
verificados por SHA-256. Reconfigurações reutilizam `build/_deps`; não é necessário
inicializar submódulos nem fazer downloads manuais. É necessária conexão HTTPS na
primeira configuração. Os arquivos baixados ficam no build, fora das fontes.
Não são compiladas as aplicações SCXT/Surge nem exemplos das dependências.

```sh
ctest --test-dir build --output-on-failure
python3 -B ci/validate.py bundle --bundle build/bin/Nord.lv2 --architecture aarch64
```

Use `--architecture x86_64` ao validar um build nessa arquitetura. Os testes
geram PCM sintético em diretórios temporários; nenhum instrumento proprietário
é necessário. Oram é validado com fixtures licenciadas da revisão `37b776a`.
Para compilar somente o produto, use `-DBUILD_TESTING=OFF`.

O resultado completo é **`build/bin/Nord.lv2/`**, com DSP, UI, metadados, catálogo
mínimo, preset interno do Pure, ferramentas de catálogo e integração Oram.
Não é necessária uma pasta `instruments`; nenhum sample é incorporado ao bundle.
A configuração e a execução não alteram o Zynthian instalado.

## Pacotes

```sh
cmake --build build --target package
cmake --build build --target package_source
```

Em Linux ARM64 nativo, o pacote binário será
`Nord-LV2-1.0.0-linux-aarch64.tar.gz`, contendo `Nord.lv2/` na raiz. Em x86-64,
o nome termina em `linux-x86_64`: esse binário não pode ser instalado no Pi.
O pacote de fontes correspondente inclui as dependências recuperadas e suas
licenças em `dependency-sources/`, permitindo reconstrução sem rede. O CMake
reconhece essa árvore automaticamente. Os fontes do port SCXT, do parser NSMP4
e dos osciladores Pure permanecem no projeto; não há dependência da aplicação
shortcircuit-xt ou de uma toolchain Rust.
A compilação cruzada pode fornecer `CMAKE_CROSSCOMPILING_EMULATOR` e
`NORD_CATALOG_HOST_EXECUTABLE` (o `nord_catalog` de um build nativo desta mesma
versão) para gerar os metadados sem executar binários ARM no host.
Compile na arquitetura/ABI de destino; o alvo inicial de distribuição é Oram
Bookworm/aarch64. Publicação, instalação e atualização são tarefas do
**Workstation Manager**, não deste repositório. A instalação local convencional,
se necessária, aceita `cmake --install build --prefix /caminho/lv2`.

## Instrumentos e presets externos

Os instrumentos não são distribuídos aqui. O mecanismo existente lê um catálogo
com caminhos externos; não foi adicionado um caminho fixo de samples ao DSP.
A ordem de resolução do catálogo permanece:

1. `NORD_LIBRARY_CATALOG`, quando configurado;
2. `$ZYNTHIAN_MY_DATA_DIR/nord-lv2/catalog.nord`, se presente;
3. `$XDG_DATA_HOME/nord-lv2/catalog.nord` ou
   `$HOME/.local/share/nord-lv2/catalog.nord`, conforme o ambiente;
4. `catalog.nord` no bundle, que contém apenas Pure em uma distribuição limpa.

Na instalação atual, a coleção está em
`/zynthian/zynthian-my-data/soundfonts/nord`. É uma configuração da instalação,
não um caminho embutido no plugin. Apenas NPNO e NSMP4 são reconhecidos;
NSMP3 e outros formatos não são convertidos implicitamente.

O Manager pode usar o **`tools/nord_catalog`** do bundle para indexar uma raiz
externa (`nord_catalog RAIZ CATALOGO --external`). O gerador de metadados
**`tools/refresh_library.py`** mantém o procedimento atual: recebe `--root`,
`--metadata` e `--bundle`, atualiza catálogo/presets sem copiar instrumentos nem
recompilar o DSP. Também respeita a configuração existente `NORD_LIBRARY_ROOT`.
São componentes internos do mecanismo de descoberta/presets, não ferramentas de
transferência ou instalação remota. Os IDs por conteúdo, índice e formatos dos
catálogos permanecem inalterados.

Após adicionar instrumentos, o Manager deve atualizar o catálogo/presets e
solicitar o rescan/recarregamento do host como já ocorre na integração existente.
O DSP não implementa um watcher da pasta de samples. Os arquivos continuam
externos e são decodificados pelo worker apenas ao selecionar o instrumento.

## Contrato com o Workstation Manager

`workstation.json` é o contrato público de instalação e Release; não substitui
`manifest.ttl`. Declara URI, bundle, plataforma, integração e as 18 pastas físicas
que o Manager deve criar, incluindo pastas vazias:

```text
Accordion       Bass             Brass          Digital Piano
E Grand Piano   Grand Piano      Guitar_Plucked Organ
Orquestral      Percussion Tuned Strings        Synth
Synth Bass      Synth Lead       Synth Pad      Upright Piano
Voices_Choir    Woodwind
```

Os nomes e a capitalização são preservados. `Organ` também é declarada quando
vazia. A apresentação de categorias pelo plugin pode transformar `_` em `/`;
a criação de diretórios usa os nomes físicos acima. A lista não limita a
descoberta: novas categorias e novos arquivos continuam sendo reconhecidos pelo
indexador existente. O build não cria nenhuma dessas pastas no Zynthian.

O Manager deve validar cada entrada de `instruments.directories` como relativa a
`base_path`, rejeitar caminhos absolutos, `..`, componentes vazios, separadores
malformados e caracteres de controle. Antes de criar qualquer pasta, deve
resolver o destino e conferir que permanece dentro da raiz, inclusive diante de
links simbólicos existentes, e recusar conflitos com arquivos. Nunca sobrescrever
arquivos para satisfazer o manifesto. Essa validação cabe ao Manager; este projeto
não implementa operações de instalação nem modifica esse aplicativo.

## Releases e validação sem publicação

A versão é SemVer **sem `v`**, mantida no código, em `CMakeLists.txt` e em
`workstation.json`. O build rejeita divergências; não edita a versão.
`.github/workflows/release.yml` compila nativamente em runner ARM64, dentro de
Debian Bookworm, instala os pacotes acima, recupera as dependências, roda CTest,
valida Turtle/LV2/ELF/bibliotecas dinâmicas, verifica o pacote extraído e gera
binário, fontes correspondentes e SHA256. A integração Oram acompanha o bundle.

Use **Actions → Nord ARM64 Release → Run workflow** para validar sem publicar.
O acionamento manual somente disponibiliza artefatos de CI. A publicação ocorre
exclusivamente em push de tag SemVer sem prefixo, idêntica à versão do manifesto,
após todas as verificações. A permissão de escrita fica restrita ao job de
publicação. O pacote binário esperado é `Nord-LV2-1.0.0-linux-aarch64.tar.gz`.
Nenhuma publicação ou instalação ocorre ao executar o CMake localmente.

## Zynthian Oram

As portas LV2 são estáticas. A filtragem dinâmica dos grupos por motor depende
dos componentes em `integrations/zynthian/`, também incluídos em `Nord.lv2/tools/`:

- `nord_host.py` / `nord_pages.py`: páginas funcionais com até quatro controles,
  sincronização com presets, filtragem por motor e navegação dos encoders;
- `configure_zynthian_nord_ui.py`: auditoria, ativação mínima com backup e rollback;
- `engine.json`: identidade e campos de classificação **MIDI Synth / Workstation**
  que o Manager aplica ao registro já descoberto pelo host, identificado pela URI.
  Não é uma cópia completa de `engine_config.json`; campos/configurações alheios
  devem ser preservados.

A auditoria é somente leitura:

```sh
python3 /caminho/Nord.lv2/tools/configure_zynthian_nord_ui.py --check --json
```

A ativação usa `--apply`; a restauração usa `--restore DIRETORIO_DO_BACKUP`.
O ativador recusa revisões incompatíveis, mantém backup e não reinicia serviços.
Atualizar somente o LV2 não carrega hooks em uma UI Python que já está em execução.
O Manager controla a ativação e o momento de recarregamento. Nenhuma integração é
aplicada automaticamente durante o build ou ao abrir o plugin.

## Licenças e procedência

O produto combinado é **GPL-3.0-or-later**. As fontes recuperadas conservam seus
avisos de copyright e os textos ISC, MIT, CC0, 0BSD e GPL; as licenças utilizadas
acompanham `Nord.lv2/licenses/`, junto do lock e deste README.

| Componente | Commit imutável / versão | Licença |
|---|---|---|
| [DPF](https://github.com/DISTRHO/DPF) | `4238e1c7f0351bbe488d79f0899c540543ac7583` | ISC |
| [Pugl](https://github.com/DISTRHO/pugl) | `5e2621d714ddf1cb0f86e852f8ba5dffe04aa3a3` | ISC / 0BSD / MIT |
| [sst-basic-blocks](https://github.com/surge-synthesizer/sst-basic-blocks) | `9cee7635944757a2bb3d80a92a1e2e0399e834d6` | GPL-3.0-or-later |
| [sst-cpputils](https://github.com/surge-synthesizer/sst-cpputils) | `67601c8d3cc7405569229b9618da55b6420633d7` | MIT |
| [sst-filters](https://github.com/surge-synthesizer/sst-filters) | `87a171bcba52c014850848a29174a54f0adc6606` | GPL-3.0-or-later |
| [sst-waveshapers](https://github.com/surge-synthesizer/sst-waveshapers) | `dd12f31a5a9016c9895e52d1a00eee0e1eebe6ce` | GPL-3.0-or-later |
| [SIMDe](https://github.com/simd-everywhere/simde) | `71fd833d9666141edcd1d3c109a80e228303d8d7` | MIT; selected support headers CC0-1.0 |
| [Shortcircuit XT curves](https://github.com/surge-synthesizer/shortcircuit-xt) | `72ad42336a8cbc2d5f74fecc303c1a48a1a5a426` | MIT |
| [nord-format](https://crates.io/crates/nord-format/0.8.0) | `0.8.0` | BSD-3-Clause |
| [Surge XT](https://github.com/surge-synthesizer/surge) | fontes identificadas por SHA-256 no lock | GPL-3.0-or-later |

As curvas SCXT adaptadas em `src/synth/scxt-port`, o port C++ de nord-format
0.8.0 (John Moore) e os helpers DPW/seno/ruído do Surge XT são código integrante
do Nord, não dependências a substituir por aplicações upstream. Seus avisos e
licenças permanecem em `src` e `licenses`. Alguns headers SIMDe declaram CC0.
A licença MIT de SCXT não substitui a GPL das bibliotecas SST.

Ao distribuir binários, disponibilize o pacote de fontes correspondente gerado
por `package_source`, incluindo os fontes das dependências fixadas e o build.
Nenhum instrumento, banco proprietário ou preset de usuário é distribuído.
