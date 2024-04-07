# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libbitbi_cli*         | RPC client functionality used by *bitbi-cli* executable |
| *libbitbi_common*      | Home for common functionality shared by different executables and libraries. Similar to *libbitbi_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libbitbi_consensus*   | Stable, backwards-compatible consensus functionality used by *libbitbi_node* and *libbitbi_wallet* and also exposed as a [shared library](../shared-libraries.md). |
| *libbitbiconsensus*    | Shared library build of static *libbitbi_consensus* library |
| *libbitbi_kernel*      | Consensus engine and support library used for validation by *libbitbi_node* and also exposed as a [shared library](../shared-libraries.md). |
| *libbitbiqt*           | GUI functionality used by *bitbi-qt* and *bitbi-gui* executables |
| *libbitbi_ipc*         | IPC functionality used by *bitbi-node*, *bitbi-wallet*, *bitbi-gui* executables to communicate when [`--enable-multiprocess`](multiprocess.md) is used. |
| *libbitbi_node*        | P2P and RPC server functionality used by *bitbid* and *bitbi-qt* executables. |
| *libbitbi_util*        | Home for common functionality shared by different executables and libraries. Similar to *libbitbi_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libbitbi_wallet*      | Wallet functionality used by *bitbid* and *bitbi-wallet* executables. |
| *libbitbi_wallet_tool* | Lower-level wallet functionality used by *bitbi-wallet* executable. |
| *libbitbi_zmq*         | [ZeroMQ](../zmq.md) functionality used by *bitbid* and *bitbi-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. Exceptions are *libbitbi_consensus* and *libbitbi_kernel* which have external interfaces documented at [../shared-libraries.md](../shared-libraries.md).

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`libbitbi_*_SOURCES`](../../src/Makefile.am) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libbitbi_node* code lives in `src/node/` in the `node::` namespace
  - *libbitbi_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libbitbi_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libbitbi_util* code lives in `src/util/` in the `util::` namespace
  - *libbitbi_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

bitbi-cli[bitbi-cli]-->libbitbi_cli;

bitbid[bitbid]-->libbitbi_node;
bitbid[bitbid]-->libbitbi_wallet;

bitbi-qt[bitbi-qt]-->libbitbi_node;
bitbi-qt[bitbi-qt]-->libbitbiqt;
bitbi-qt[bitbi-qt]-->libbitbi_wallet;

bitbi-wallet[bitbi-wallet]-->libbitbi_wallet;
bitbi-wallet[bitbi-wallet]-->libbitbi_wallet_tool;

libbitbi_cli-->libbitbi_util;
libbitbi_cli-->libbitbi_common;

libbitbi_common-->libbitbi_consensus;
libbitbi_common-->libbitbi_util;

libbitbi_kernel-->libbitbi_consensus;
libbitbi_kernel-->libbitbi_util;

libbitbi_node-->libbitbi_consensus;
libbitbi_node-->libbitbi_kernel;
libbitbi_node-->libbitbi_common;
libbitbi_node-->libbitbi_util;

libbitbiqt-->libbitbi_common;
libbitbiqt-->libbitbi_util;

libbitbi_wallet-->libbitbi_common;
libbitbi_wallet-->libbitbi_util;

libbitbi_wallet_tool-->libbitbi_wallet;
libbitbi_wallet_tool-->libbitbi_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class bitbi-qt,bitbid,bitbi-cli,bitbi-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Consensus* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libbitbi_wallet* and *libbitbi_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libbitbi_consensus* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libbitbi_util* should also be a standalone dependency that any library can depend on, and it should not depend on other internal libraries.

- *libbitbi_common* should serve a similar function as *libbitbi_util* and be a place for miscellaneous code used by various daemon, GUI, and CLI applications and libraries to live. It should not depend on anything other than *libbitbi_util* and *libbitbi_consensus*. The boundary between _util_ and _common_ is a little fuzzy but historically _util_ has been used for more generic, lower-level things like parsing hex, and _common_ has been used for bitbi-specific, higher-level things like parsing base58. The difference between util and common is mostly important because *libbitbi_kernel* is not supposed to depend on *libbitbi_common*, only *libbitbi_util*. In general, if it is ever unclear whether it is better to add code to *util* or *common*, it is probably better to add it to *common* unless it is very generically useful or useful particularly to include in the kernel.


- *libbitbi_kernel* should only depend on *libbitbi_util* and *libbitbi_consensus*.

- The only thing that should depend on *libbitbi_kernel* internally should be *libbitbi_node*. GUI and wallet libraries *libbitbiqt* and *libbitbi_wallet* in particular should not depend on *libbitbi_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be get able it from *libbitbi_consensus*, *libbitbi_common*, and *libbitbi_util*, instead of *libbitbi_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libbitbiqt*, *libbitbi_node*, *libbitbi_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](`../../src/interfaces/`) abstract interfaces.

## Work in progress

- Validation code is moving from *libbitbi_node* to *libbitbi_kernel* as part of [The libbitbikernel Project #24303](https://github.com/bitbi/bitbi/issues/24303)
- Source code organization is discussed in general in [Library source code organization #15732](https://github.com/bitbi/bitbi/issues/15732)
