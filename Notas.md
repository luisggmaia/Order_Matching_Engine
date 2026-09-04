

# Enunciado – Order Matching Engine/System


##############################################

O uso de ferramentas de Inteligência Artificial é permitido. No entanto, o candidato é integralmente responsável pelo código entregue e deve conhecer e conseguir explicar toda a base de código, incluindo decisões técnicas, comportamentos, limitações e testes. Código produzido com o auxílio de IA será considerado, para todos os efeitos da avaliação, como código de autoria e responsabilidade do candidato.

O histórico Git é importante para nós e fará parte da avaliação. Mantenha commits incrementais, com mensagens claras, que demonstrem a evolução da solução e as decisões tomadas. Evite concentrar toda a implementação em um único commit final.

Todos os requisitos descritos abaixo, incluindo os requisitos adicionais, são obrigatórios.

##############################################

Uma ordem, no mercado financeiro, significa uma manifestação de interesse de compra ou venda de um determinado ativo. Para fins de simplicidade, iremos trabalhar com dois tipos de ordem: uma ordem a mercado (ou Market Order) e uma ordem limite (ou Limit Order).

Uma Matching Engine (ou Order Matching System) é um sistema desenvolvido para cruzar ordens em uma exchange de forma **rápida e justa**. O seu objetivo é estruturar uma matching engine simples, de acordo com algumas premissas:

1- A engine trabalhará com apenas 1 ativo

2- As ordens possíveis são limit (uma ordem passiva colocada a um preço fixo) e market (uma ordem que deve ser preenchida no melhor preço disponível imediatamente)

3- Não é necessário ter armazenamento perene das ordens e trades; todas as informações podem ser mantidas em memória volátil

4- Não é necessário pensar em escalabilidade de hardware, ferramentas de nuvem ou elasticidade (como Kubernetes)

O objetivo é desenhar um projeto de software utilizando **formas eficientes de estruturação de dados, algoritmos e engenharia de software**. O software pode ser escrito de forma estrutural ou orientada a objetos.

 

Deve ser possível inserir ordens com as informações:

- Tipo (limit/market)

- Side (buy/sell)

- Price (quando a ordem for limit)

- Qty

Limit orders com preços que gerariam trades podem ser ignoradas ou preenchidas, porém o comportamento escolhido deve ser justificado.

Quando um trade for realizado, deve-se mostrar na saída:

`Trade, price: <preço do trade>, qty: <número de shares>`

 

Exemplos de entrada e saída:

```

>>> limit buy 10 100

>>> limit sell 20 100

>>> limit sell 20 200

>>> market buy 150

Trade, price: 20, qty: 150

>>> market buy 200

Trade, price: 20, qty: 150

>>> market sell 200

Trade, price: 10, qty: 100

```

 

Requisitos adicionais obrigatórios:

1. Implementar uma função/método para visualização do livro;

2. Respeitar a ordem de chegada das ordens. No exemplo anterior, isso significa que a primeira ordem de venda, com quantidade 100, deve ser preenchida antes da segunda, com quantidade 200;

3. Implementar cancelamento. Uma ordem, ao ser cancelada, deve ser retirada da matching engine. Exemplo:

```

>>> limit buy 10 100

Order created: buy 100 @ 10 identificador_1

>>> cancel order identificador_1

Order cancelled

```

4. Implementar alteração de ordem. Uma ordem alterada tem seu preço, quantidade ou ambos modificados. Considerando o primeiro requisito adicional, lembre-se de que uma ordem com alteração de preço deve ser recolocada na faixa de preço adequada. Em um livro hipotético:

```

Ordens de Compra    | Ordens de Venda

-------------------|-----------------

200 @ 10            | 100 @ 10.5

100 @ 9.99        |

```

Ao alterar a primeira ordem de compra (200 unidades ao preço de R$ 10,00) para um preço de 9.98, devemos ter a seguinte configuração do livro:

```

Ordens de Compra    | Ordens de Venda

--------------------|-----------------

100 @ 9.99          | 100 @ 10.5

200 @ 9.98          |

```

Ou seja, perdeu prioridade na fila.

5. Uma ordem pegged é um tipo de ordem que segue um determinado preço de referência. Bid é o melhor preço de compra disponível no livro de ofertas. Offer é o melhor preço de venda. Por exemplo, uma ordem *peg to the bid* irá acompanhar o preço do bid, ou seja, terá sempre o preço atualizado pela matching engine para acompanhar o melhor preço de compra, conforme o exemplo abaixo:

```

>> print book

Ordens de Compra    | Ordens de Venda

--------------------------|-----------------

200 @ 10                       |    100 @ 10.5

100 @ 9.99                   |

```

```

>> peg bid buy 150

Ordens de Compra    | Ordens de Venda

--------------------------|-----------------

200 @ 10                        |  100 @ 10.5

150 @ 10                        |

100 @ 9.99                    |

```

```

>> limit buy 10.1 300

Ordens de Compra    | Ordens de Venda

--------------------------|-----------------

150 @ 10.1                    |   100 @ 10.5

300 @ 10.1                    |

200 @ 10                        |

100 @ 9.99                    |

```

O mesmo funciona para uma ordem peg to offer.

##############################################


# Notas / Gagá


## Order Types

- Limit Order
    - Limit orders allow the buyer to define the maximum purchase price for buying an instrument and the seller to define the minimum sale price for selling an instrument.

- Market-limit Order
    - Market-limit orders are executed at the best price available in the market. If the market-limit order can only be partially filled, the order becomes a limit order and the remaining quantity remains on the order book at the specified limit price.


## Algoritmos

https://cmegroupclientsite.atlassian.net/wiki/spaces/EPICSANDBOX/pages/457218479/Supported+Matching+Algorithms

*Agressor*: "An "Aggressor" or "Aggressing Order" by definition is an incoming order matching with one or more orders resting on the order book"

### FIFO – Price/Time

Usa preço e tempo como único critério para preencher uma ordem. Dentre todas as ordens no *order-book* de um mesmo lado (*buy* ou *sell*), os melhores preços (maiores de buy e menores de sell) são executados primeiro. Entre ordens de mesmo preço, executa-se primeiro as que foram emitidas primeiro.

"In this algorithm, all orders at the same price level are filled according to time priority; the first order at a price level is the first order matched.

It is important to note that an order loses order priority and is re-queued when changed in any of the following ways:
- Increase the quantity
- Change the price
- Change the account number"

#### Com LMM (Lead Market Maker)

Dá alocação preferencial para *Lead Market Makers*

Não será o caso para o projeto, já que não haverá uma descrição de tais LMMs

### Pro-Rata

"This step fills by proportion of the working order quantity compared to the entire quantity present at the price level at the time of a match. Therefore, larger orders will receive a larger percentage of a fill in this step.

Pro Rata is never the last step of an algorithm due to the required rounding; the Pro Rata step will always be followed by either a FIFO step or Leveling and FIFO steps."

### Time-Weighted Average Price (TWAP)



### Alocação

Pro-Rata aprimorado; "The Allocation algorithm is an enhanced pro-rata algorithm that incorporates a priority (top order) to the first incoming order that betters the market."


### C++

- O que é um container
- Uso de `template`

# Cronograma

- Segunda: gagá, requisitos etc, rascunho da Engine
- Terça: gagá, implementação:
    - Implementar InsertionOrderMap
    - Implementar Order
    - Implementar OrderBook
- Quarta: implementação
- Quinta: gagá, reavaliação
- Sexta:
- Sábado:
- Domingo:

# Detalhes da solução

Implementação em C++

Classe para uma ordem. Deve conter Order ID, o Type (*Buy* ou *sell*), a Qty, o Price (), o status, a hora (?), ...

Claro que tal poderia conter diversos outros elementos (dados do cliente etc), mas não será relevante.

Lista (?) para a ordenação das ordens

E o preço de abertura? Digo: um erro há de se levantar quando a primeira oferta é Market-limit?

O sistema precisa ser sequencial (*single-threaded*).

CME Group: seguem steps: TOP $\rightarrow$ Pro-Rate $\rightarrow$ FIFO

Duas implementações possíveis:
- Sejam $n$ o número de preços distintos, $m$ o número de ordens ($n \geq m$) e $m_i$ o número de ordens com preço de índice $i$.
- Fila/Queue para armazenamento ordenado das ordens
    - Inserção ordenada: $\mathcal{O}(m)$ no pior caso, $\mathcal{O}(1)$ no melhor.
    - Consulta elementar:
    - Consulta completa: linear (fácil). Iteração linear ao longo da Fila/Lista.
- R-B-Tree/Map para o ordenamento dos preços e Fila/Queue para o armazenamento das ordens
    - Inserção ordenada: $\mathcal{O}(\log{n})$.
    - Consulta elementar: depende de como são atribuídos os IDs. Se de uma forma lógica (e. g. respeitando a ordenação), a consulta elementar variará entre $\mathcal{O}(\log{n})$ e $\mathcal{O}(m_i \log{n})$.
    - Consulta completa: linear (também fácil). Iteração linear ao longo do Map e iteração linear ao longo de cada Fila

O tempo (para priorização) será medido em número de ordens. A cada nova ordem, a variável (int) de tempo será acrescida. Viso economia de memória, tendo em vista um número grande de ordens diárias – embora não mais que $2^{32}$.

# Dúvidas

1. listas diferentes para Types diferentes? Ao invés de guardar o type na estrutura.
1. O que acontece quando uma ordem *Peg* torna-se a de melhor preço, após a anterior de melhor preço ter sido completamente executada? Rigorosamente pela definição, creio que tem seu preço atualizado para o (antes) segundo melhor preço.
1. O que é mais rápido/eficiente (inclusive em termos de memória): Map ou Set com comparativo na classe.
1. Uma ordem alterada tem sua prioridade (por respeito à ordem de chegada) alterada? Digo: a sua hora é atualizada (e, portanto, precisaria ser redisposta na Fila)?
    - Conforme o CME Group, a prioridade é alterada, de fato, se houver mudança consistida de: 1. aumento da sua quantidade; 2. mudança de preço.
    - Infiro, pois, que diminuindo a quantidade não há alteração na prioridade. Preciso de alguma outra fonte de confirmação. E de uma lógica que explique isso.
1. Como devem funcionar ordens *peg bid sell* e *peg offer buy*? São liquidadas imediatamente?

# Respostas

1. Uma ordem *Peg* estará sempre na primeira Fila. E deve ser atualizada constantemente. Assim que uma mudança for feita, pode-se: 1. procurar por todas as ordens *Peg* na Fila corresepondente e atualizá-las, se for o caso.

1. Outra possibilidade: manter uma Fila separada, somente para as ordens *Peg* (embora ainda atrelada à primeira posição no Map/na árvore ordenada). Isso facilitará a sua atualização. Mas complicará um pouco a manutenção da prioridade nas duas filas consequentes (a de melhor preço e a de *Peg*).

1. Para uma oferta que supera os melhores preços, a lógica é de executá-las ao melhor preço corrente. A lógica é de que o operador conhece o livro de ofertas (hipótese 1) e sabe os melhores preços correntes. Sob a hipótese de ser racional (hipótese 2), se oferta um preço melhor, é porque quer ter maior prioridade frente a todas as outras ofertas. E é esperado que ofertará uma quantidade maior que aquela ofertada no melhor preço corrente, de modo que um novo melhor preço deverá ser alcançado. Por isso ofertou mais. Não faria sentido que ele ofertasse mais que o melhor preço, sabendo-o, se não fosse por essa razão. (melhorar explicação).

1. Quanto a 

# Requisitos

Morgan:
1. Apenas 1 ativo
1. Ordens *Limit*, *Market-limit* e *Peg*
1. Sem necessidade de armazenamento do *order book*
1. Eficiência (com algoritmos, estruturas de dados)
1. Deve ser possível inserir ordens com as informações: Tipo (limit/market); Side (buy/sell); Price (quando a ordem for limit); Qty.
1. Limit orders com preços que gerariam trades podem ser ignoradas ou preenchidas.
1. Quando um trade for realizado, deve-se mostrar na saída: `Trade, price: <preço do trade>, qty: <número de shares>`
1. Implementar uma função/método para visualização do livro;
1. Respeitar a ordem de chegada das ordens.
1. Implementar cancelamento. Uma ordem, ao ser cancelada, deve ser retirada da matching engine.
1. Implementar alteração de ordem. Uma ordem alterada tem seu preço, quantidade ou ambos modificados. Uma ordem com alteração de preço deve ser recolocada na faixa de preço adequada

Resumo:
- Estrutura de dados com:
    - Ordenação
    - Consulta elementar por ID $\rightarrow$ cancelamento/alteração
    - Inserção/remoção
    - Consulta completa ordenada
    - Atualizar ordens *Peg* constantemente.

Meus:
1. Implementar em C++
1. FIFO – consequência do respeito à ordem

### Funções

- Criar nova ordem.
    - No fundo: construtor da classe;
    - (A criação dar-se-á provavelmente no programa principal)
- Atualizar o livro em loop, até o alcançe de novo equilíbrio.
    - *Deve receber como entrada uma nova ordem.
- Consultar/localizar ordem por ID.
    - Eficiência: não percorrer toda a estrutura para a localização.
    - Para eficiência – evitar-se percorrer toda a estrutura –, os IDs devem serguir uma lógica.
- Cancelar ordem por ID:
    - localizar ordem;
    - remover da Fila;
    - remover o preço do Map, se for a sua última ordem? Ou ponteiro nulo?
    - atualizar ordens *Peg*
- Atualizar ordem por ID (somente Price ou Qty):
    - localizar ordem;
    - verificar diminuição de Qtd: se sim, apenas a altera, sem mudar preferência – conclusão/break; se não, segue o fluxo;
    - atualizar a hora;
    - atualizar a Qty;
    - atualizar o preço; se sim, muda a posição no Map;
    - remover da Fila;
    - inserir na Fila (útima posição);
    - atualizar o livro.
- Printar livro

# Rascunho

Suponha um equilíbrio com melhor bid $b$ e melhor offer $s$, $s > b$.

Considere que as ordens *Peg* são uma fila à parte (tanto para bid quanto para offer). Tal será utilizada, pois, somente no momento de execução, quando uma oferta com melhor preço que os disponíveis é criada. Não será preciso, pois, atualizar tais ordens sempre que um novo melhor preço for alcançado no equilíbrio.

### Simplificado – inserir diretamente

Chega uma nova ordem bid a preço $p$.
- Então a comparação com $b$ é $\mathcal{O}(\log{n})$. Localiza-se o preço no Map.
    - Insere-se a ordem na fila correspondente, por último.
    - Atualiza-se/executa-se as ordens em sequência.

### Evitar inserir diretamente

Chega uma nova ordem bid a preço $p$.
- Então a comparação com $b$ é $\mathcal{O}(\log{n})$. Localiza-se o preço no Map.
    - Se $p \leq b$: insere na fila correspondente, por último.
    - Se $b < p < s$: adiciona o preço (se não existir), adiciona à fila.
    - Se $s \leq p$: executa as ordens *Peg*:
        - Se não forem executadas totalmente, insere a nova ordem à fila no novo melhor preço.
        - Se forem executadas totalmente, executa a nova ordem.
            - Se for executada totalmente, atualiza/executa em sequência
            - Se não, insere a nova ordem à fila no novo melhor preço.

### ID

Creio que o ID possa ser uma combinação (junção "{Price}_{Time}") entre o Price e o Time, a fim de tornar eficiente a busca. A 

### Fila

Duas possibilidades:
- Fila pura:
    - Localização: tempo $\mathcal{O}(n)$, memória $\mathcal{O}(1)$
- Fila com unordered_map (armazenando ponteiros para os elementos)
    - Localização: tempo $\mathcal{O}(1)$, memória $\mathcal{O}(n)$

### Tempo

Duas possibilidades:
- Float, hora do sistema
- Long (Int), acrescida a cada nova ordem inserida

### InsertionOrderMap

Requisitos
- Emplace
- Push
- Pop
- Find
- Somente o necessário; voltado especificamente para essa aplicação

## Metodologia 1

### Propriedades

- Map para armazenamento ordenado, com base nos preços (como chaves), das filas;
- Fila – Insertion Order Map: Lista (Fila) com Map para busca a $\mathcal{O}(1)$;
- Tempo: int
- Order: struct
    - int time
    - float price
    - int qty
    - Propriedades: tipo (market, limit, peg), side (buy, offer)
- OrderBook: class
- Desconsidera ordens *peg bid sell* e *peg offer buy*

Nesse caso, qualquer localização de preço tem complexidade $\mathcal{O}(\log{n})$. Inclusive para o máximo/mínimo.

## Metodologia 2

### Propriedades

- Ordered Map para armazenamento ordenado com consulta a $\mathcal{O}(1)$;
- Fila – Insertion Order Map
- Histórico das ordens e seu status

# Referências

- CME Group: https://cmegroupclientsite.atlassian.net/wiki/spaces/EPICSANDBOX/pages/457218479/Supported+Matching+Algorithms, acessado em 31/08.
- Artigo The Order Matching Engine: Price-Time Priority, Order Books, and Throughput Optimization (Medium): https://hosseinnejati.medium.com/the-order-matching-engine-price-time-priority-order-books-and-throughput-optimization-de5badb936f9, acessado em 31/08.
