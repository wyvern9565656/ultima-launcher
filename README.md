# Ultima Launcher

Gerenciador de jogos para Linux com interface gráfica (CustomTkinter), com foco em jogos Windows rodando via Wine/Proton e jogos nativos.

## Funcionalidades

- Cadastro de jogos com executável, capa, ícone e comando de lançamento.
- Suporte a Wine/Proton, incluindo prefixos customizados e modo debug em terminal.
- Rastreamento de sessões e tempo de jogo (`dados/sessions.json`).
- Integração com o SteamGridDB para buscar capas e imagem no Discord.
- Discord Rich Presence (requer Discord Desktop aberto).
- Sons configuráveis para eventos da interface.
- Organização por capas/ícones copiados para a pasta do launcher (mover o original não quebra o jogo).
- Busca de capas já presentes na pasta de instalação do jogo (helper `coverscan`).
- Rastreio de processos da sessão para encerramento seguro (helper `procscan`).
- Interface bilíngue (português/inglês).

## Requisitos

- Linux.
- Python 3 com `tkinter`.
- Dependências Python: `customtkinter`, `Pillow` (opcional, para bandeja), `pystray` (opcional).
- `wine`/`proton`/`umu-run` para jogos Windows (conforme o jogo).
- Compilador Rust (`rustc`) para os helpers em `rust/` (opcional, mas recomendado).

## Instalação e execução

```bash
cd ~/UltimaLauncher
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt   # se existir
python3 launcher.py
```

Ou use o script auxiliar:

```bash
./ultimalauncher.sh
```

Os diretórios de dados (`dados/`, `capas/`, `imagens/`, `icones/`, `logs/`, `rust/`) são criados automaticamente na primeira execução em `~/UltimaLauncher`.

## Estrutura

- `launcher.py` — aplicação principal.
- `assets/` — recursos da interface.
- `rust/` — helpers em Rust (`procscan`, `coverscan`) e suas fontes `.rs`.
- `raylib/` — variante experimental em C++ (raylib). Ver nota abaixo.

## Nota sobre a variante em C++ (raylib)

A pasta `raylib/` contém uma tentativa de remake do launcher em C++ usando raylib. Esse experimento foi **deixado de lado**: o launcher principal em Python já está maduro e atende às necessidades. O código em `raylib/` é mantido apenas como referência/histórico e não é usado pela versão principal.

## Licença

Uso pessoal. (Ajuste conforme sua preferência.)
