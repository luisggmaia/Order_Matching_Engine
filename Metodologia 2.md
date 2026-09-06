# Metodologia 2

# Propriedades

1. `int time`
    -
    Tempo inteiro;
    
    Variável do OrderBook
    
    Contador que é acrescido a cada nova ordem que é adicionada

    Tipo `int` é suficiente; suponho menos de $2\ bi$ de ordens. Além de ser eficiente em tempo e memória.

1. `struct Order`
    -
    { `Type type`, `Side side`, `int qty`, `int price`, `int time` }

    `type` *Pegged* ou *Limit* ou *Market*

    `side` *Buy* ou *Sell*

    `time` para ordenação em comparação com ordens *Pegged*.

    `qty` quantidade de *shares*. Suponho `int` suficiente.

    `price` em tipo `int`. Assume-se $2$ casas decimais. Portanto, a conversão para float é necessária apenas nas bordas de entrada e saída. Não será necessário em ordens *pegged*. Nesse caso, vou assumir `price = 0`.

    A struct é mais econômica que uma classe. Além de que funções próprias não são necessárias. O *id* é também dependente do *type* da ordem e do seu *side*. Assim, a menos que tais fossem também atributos da classe – o que exigiria maior consumo de memória, embora permitisse funções próprias –, a sua obtenção é suficiente como uma propriedade/capacidade do *book*. Afinal, um *id* faz sentido somente no contexto de um *book*.

1. `enum class Type`
    -

1. `enum class Side`
    -

1. *id*
    -
    `int`

    Igual ao `time` para cada ordem.

1. `class OrderBook`
    -
    `private:`
        
        std::unordered_map<Side, std::map<float, InsertionOrderMap<float, Order>>> book;

        unordered_map permite utilizar o *side* para acessar os map

    `void insert_order(Type type, Side side, )`:


    `void change_order(Type type, Side side, )`:

        Vai mudar tudo: `qty` e `price` (além do `time`).

1. Map para armazenamento ordenado, com base nos preços (como chaves), das filas

1. Fila – Insertion Order Map: Lista (Fila) com Map para busca a $\mathcal{O}(1)$

1. Desconsidera ordens *peg bid sell* e *peg offer buy*

1. Quando ordens *limit* são todas liquidadas antes das *pegged*, nada é feito até que uma nova *limit* seja criada, para referenciar o preço das *pegged*.

    "Isso é uma simplificação legítima e bastante usada até em sistemas reais (algumas implementações chamam isso de peg "unpriced" ou "inactive" quando não há NBBO/referência válida). Não é gambiarra, é uma escolha de design defensável — a alternativa (ex.: usar o último preço negociado como fallback, ou um preço "far peg" de proteção) adiciona complexidade que você não precisa para uma primeira versão."

1. Saídas

    1. Quando uma ordem for criada

        Retornar o *id* da ordem e a confirmação da criação.

        `bool`
    
    1. Quando uma ordem for modificada

        Retornar o novo *id* da ordem e a confirmação da criação

        `bool`
    
    1. Quando uma ordem for cancelada

        Retornar a confirmação do cancelamento

        `bool`
    
    1. Quando uma ordem for executada

        Retornar o preço e a quantidade executada;

        Retornar os *id*s das ordens executada (*Buy* e *Sell*); e se total ou parcial;

        ``
    
1. Erro quando uma *Market* é inserida sem que haja uma *limit*

    Futura implementação: adotar uma fila de espera para ordens nesse caso.

1. Política de preço

    Para uma oferta que supera os melhores preços, a lógica é de executá-las ao melhor preço corrente. A lógica é de que o operador conhece o livro de ofertas (hipótese 1) e sabe os melhores preços correntes. Sob a hipótese de ser racional (hipótese 2), se oferta um preço melhor, é porque quer ter maior prioridade frente a todas as outras ofertas. E é esperado que ofertará uma quantidade maior que aquela ofertada no melhor preço corrente, de modo que um novo melhor preço deverá ser alcançado. Por isso ofertou mais. Não faria sentido que ele ofertasse mais que o melhor preço, sabendo-o, se não fosse por essa razão. (melhorar explicação).

1. Print book

    Retornar duas (uma para *Buy* e outra para *Sell* sides.) listas de iteradores (`std::list<std::list<Order>::iterator>`), com os ponteiros para os elementos, na ordem de print.

1. Ainda vou remover o elemento do map quando não houver mais ordens para um dado preço

1. Update book

    Os trades feitos são guardados num buffer: uma lista

# Código

```
class OrderBook {
    private:
        std::unordered_map<Side, std::map<float, InsertionOrderMap<float, Order>>> book;
        std::unordered_map<Side, InsertionOrderMap<float, Order>> peg_book;
        int time;

    public:
        OrderBook();
        void update_book();
        void print_book();
        void insert_order();
        void remove_order();
        void locate_order(std::string id);
        void change_order();
        std::string get_order_id();
        std::string get_order_status(); // ?
};
```





