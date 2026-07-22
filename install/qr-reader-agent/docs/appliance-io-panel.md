# Painel de campo (relé + sensor de porta)

Layout mínimo para o **protótipo funcional**: bornes para fora da case, sem fio direto no header do Pi.

## Ideia

| Saída de campo | Função | Pi (BCM) |
|----------------|--------|----------|
| `LOCK` (borne do módulo relé) | Pulso da trava | GPIO **17** (IN do módulo) |
| `DOOR` (2 vias) | Reed MC38 (contato seco) | GPIO **4** + **GND** |

O pull-up do reed é **interno** no GPIO. Resistor série 1 kΩ no `DOOR SIG` é opcional (footprint na placa).

## Placa do sensor (`door-terminal`)

Plaquinha só para o borne do reed, com furos de parafuso (como o módulo de relé).

**Não precisa comprar FR4 30×20 pronto** — quase ninguém vende nesse tamanho. Corte de uma placa maior ou use uma das opções abaixo.

| Dimensão | Valor |
|----------|-------|
| Placa alvo | **30 × 20 mm** (pode ser um pouco maior) |
| Furos | **2× Ø 3,2 mm** (M3), centros a **20 mm** |
| Margem do furo ao canto | 5 mm (eixo Y centrado: y = 10 mm) |
| Borne | **KF301 / Phoenix 5,08 mm**, 2 vias, na borda longa voltada para fora da case |
| Passagem interna | pads ou bornes menores / solda para pigtail Dupont → Pi |

### Onde achar material (protótipo)

| Opção | O que comprar | Como fazer |
|-------|---------------|------------|
| **Melhor custo** | Protoboard / perfboard **5×7 cm** ou **7×9 cm** (furos 2,54 mm) | Imprima o SVG 1:1, cole por cima, corte com serra/estilete + régua, fure M3 |
| **Já com borne** | Módulo “screw terminal 2P” / breakout 2 vias (AliExpress, ML, etc.) | Use a placa do módulo; se os furos não baterem, alargue ou faça 2 furos novos a 20 mm |
| **Sem PCB** | Suporte 3D / acrílico 2–3 mm 30×20 + KF301 | Parafuse o borne no suporte (cola epoxy ou parafuso no corpo, se tiver orelha) |
| **FR4 sobra** | Qualquer PCB velha / copper clad | Corte 30×20 (ou 35×25), fure, solde o KF301 |

Espessura: **1,2–1,6 mm** de protoboard já serve. FR4 1,6 mm é o padrão de PCB, não um requisito rígido.

Se o corte ficar **35×25** ou **40×25**, sem problema: o que importa é o **espaçamento dos furos (20 mm)** e o borne na borda da case.

### Furos (origem = canto inferior esquerdo da placa)

| Furo | X (mm) | Y (mm) |
|------|--------|--------|
| A | 5 | 10 |
| B | 25 | 10 |

### Bornes / silkscreen

| Label | Destino |
|-------|---------|
| `DOOR SIG` | GPIO 4 (pino físico 7), opcional 1 kΩ em série |
| `DOOR GND` | GND (pino físico 9) |

Sem polaridade no MC38: qualquer fio do reed em `SIG` e o outro em `GND`.

### SVG 1:1 (imprimir em 100%)

Arquivo: [`door-terminal-board.svg`](./door-terminal-board.svg) — escala 1 mm = 1 mm.

## Montagem na case

1. Parafuse a placa do sensor com **2× M3** (standoff 6–10 mm se precisar folga do fundo).
2. Alinhe a face do borne com a **mesma parede** do borne `NO/COM` do relé (instalador vê `LOCK` + `DOOR` juntos).
3. Internamente: pigtail curto (≤ 15 cm) da placa → GPIO 4 e GND. Não use o cabeçalho do Pi como conector de campo.
4. Rótulos na case: `LOCK` (carga 12 V) e `DOOR SENSOR` (sinal seco). Não misture no mesmo bloco sem divisão clara.

### Espaçamento sugerido na parede

```
        ┌──────────────── case wall ────────────────┐
        │  [ LOCK NO ] [ LOCK COM ]   [ DOOR SIG ] [ DOOR GND ]
        │   ← módulo relé existente →   ← placa 30×20 →
        └────────────────────────────────────────────┘
```

Centro a centro entre o bloco do relé e o do sensor: **≥ 25 mm** (dedo + chave de fenda).

## BOM rápido

| Item | Qtd | Nota |
|------|-----|------|
| KF301 2P (5,08 mm) | 1 | campo |
| Protoboard 5×7 (cortar) | 1 | ou breakout 2P pronto; ver tabela acima |
| Parafuso M3 + porca/standoff | 2 | mesmos do relé se possível |
| Resistor 1 kΩ 0805/axial | 0–1 | opcional em série no SIG |
| Fio AWG 22–24 | — | pigtail interno |

## Wiring elétrico

```
MC38 ──► DOOR SIG ──(1kΩ opcional)──► GPIO 4
MC38 ──► DOOR GND ──────────────────► GND
```

Porta fechada (NF): contato fecha → linha em LOW (com pull-up interno) → agent `closed`.
