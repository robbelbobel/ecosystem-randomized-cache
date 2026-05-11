error id: 1A754CBFB7C0AFAF527950932CFD2157
file://<WORKSPACE>/core/src/main/scala/riscv/plugins/Cache.scala
### java.util.NoSuchElementException: head of empty list

occurred in the presentation compiler.



action parameters:
offset: 1950
uri: file://<WORKSPACE>/core/src/main/scala/riscv/plugins/Cache.scala
text:
```scala
package riscv.plugins

import riscv._
import spinal.core._
import spinal.lib._
import riscv.BaseIsa.RV32E.xlen
import scala.util.Random
import spinal.core.sim.SimDataPimper

object ReplacementPolicy extends SpinalEnum {
  val RPLRU, RAN = newElement() // Pseudo-LRU, Random
}

object SkewApproach extends SpinalEnum {
  val RS, LA = newElement() // Random Selection, Load Aware
}

object EvictionPolicy extends SpinalEnum {
  val LE, GE = newElement() // Local Eviction, Global Eviction
}

class Cache(
    sets: Int,
    ways: Int,
    skews: Int,
    busFilter: ((Stage, MemBus, MemBus) => Unit) => Unit,
    prefetcher: Option[PrefetchService] = None,
    maxPrefetches: Int = 1,
    cacheable: (UInt => Bool) = (_ => True),
    randomizedSetIndexing: Bool =
      True, // TODO: Enforce this better: Disable all features related to randomized caching
    replacementPolicy: ReplacementPolicy.E = ReplacementPolicy.RPLRU,
    skewApproach: SkewApproach.E = SkewApproach.LA,
    invalidTags: Int = 0, // Invalid Tags
    evictionPolicy: EvictionPolicy.E = EvictionPolicy.GE,
    delay: Int = 1
)(implicit config: Config)
    extends Plugin[Pipeline] {
  // Verify Security Options
  assert(skews >= 1, "Cache must exist out of 1 or more skews")
  assert(
    invalidTags < sets * ways * skews,
    "Invalidtags cannot be larger than the total amount of ways in the cache."
  )

  private val byteIndexBits = log2Up(config.xlen / 8)
  private val wordIndexBits = log2Up(config.memBusWidth / config.xlen)
  private val setIndexBits = log2Up(sets)

  // RNG
  private val rngState = RegInit(U(BigInt(config.xlen * 2, scala.util.Random), config.xlen * 2 bits))
  private val rngMutliplier = BigInt(config.xlen * 2, scala.util.Random)
  private val rngIncrement = BigInt(config.xlen * 2, scala.util.Random)

  private def rngPCG() : UInt = {
    val count = rngState >> 59 // 64 - 59 = 5 -> 5 bit rotation (32 bit possible rotations)

    rngState = rng@@
  }

  // Set Index Bits Must Be Divisible By 2 (Needed for Feistel Algorithm)
  assert(sets > 0 && setIndexBits % 2 == 0, "Set index bits must be divisable by 2")

  // Initialize Key (4 Stages like CAESER)
  private val feistelStages = 4
  private val key = Vec.fill(feistelStages)(Reg(UInt(setIndexBits / 2 bits)))

  for (i <- 0 until feistelStages) {
    key(i) := scala.util.Random.nextInt(setIndexBits / 2)
  }

  private case class CacheEntry() extends Bundle {
    val tag: UInt = UInt(config.xlen - (byteIndexBits + wordIndexBits + setIndexBits) bits)
    val value: UInt = UInt(config.memBusWidth bits)
    val age: UInt = UInt(log2Up(sets * skews * ways) bits)
    val valid: Bool = Bool()
  }

  case class WayResult() extends Bundle {
    val set: UInt = UInt(log2Up(sets) bits)
    val skew: UInt = UInt(log2Up(skews) bits)
    val way: UInt = UInt(log2Up(ways) bits)
  }

  class SkewUsage() extends Bundle {
    val skew: UInt = UInt(log2Up(skews) bits)
    val usage: UInt = UInt(log2Up(ways) bits)
  }

  private def getSetIndex(address: UInt): UInt = {
    if (randomizedSetIndexing == True && (setIndexBits % 2) == 0) {
      val half = setIndexBits / 2

      // 4-Stage Feistel-Network
      var L = address(byteIndexBits + wordIndexBits, half bits)
      var R = address(byteIndexBits + wordIndexBits + half, half bits)

      for (i <- 0 until feistelStages) {
        var temp = R ^ key(i)
        R = L
        L = temp
      }

      (L ## R).asUInt
    } else {
      // Default to Standard Set-Associative Indexing
      address(byteIndexBits + wordIndexBits, setIndexBits bits)
    }
  }

  private def getTagBits(address: UInt): UInt = {
    address(byteIndexBits + wordIndexBits + setIndexBits until config.xlen)
  }

  // get all address bits that determine whether two addresses fall into the same cache line
  private def getSignificantBits(address: UInt): UInt = {
    U(getTagBits(address) ## getSetIndex(address))
  }

  private def connect(_s: Stage, internal: MemBus, external: MemBus): Unit = {
    val cacheArea = pipeline plug new Area {
      private val totalWays: Int = ways * skews * sets

      private val idWidth = internal.config.idWidth
      private val maxId = UInt(idWidth bits).maxValue.intValue()

      private val cache =
        Vec.fill(sets)(Vec.fill(skews)(Vec.fill(ways)(RegInit(CacheEntry().getZero))))

      private val cacheHits = RegInit(UInt(config.xlen bits).getZero)
      private val cacheMisses = RegInit(UInt(config.xlen bits).getZero)
      private val forwardedLoads = RegInit(UInt(config.xlen bits).getZero)
      private val validTags =
        RegInit(
          UInt(config.xlen bits).getZero
        ) // Tracks the amount of valid tags currently in this cache

      private val externalId = RegInit(UInt(external.config.idWidth bits).getZero)

      private val storeInCycle = Bool()
      storeInCycle := False

      private val outstandingPrefetches = RegInit(UInt(log2Up(maxPrefetches + 1) bits).getZero)
      private val incrementOutstandingPrefetches = Bool()
      private val decrementOutstandingPrefetches = Bool()
      incrementOutstandingPrefetches := False
      decrementOutstandingPrefetches := False

      // this logic is to avoid problems when incrementing and decrementing in the same cycle
      when(incrementOutstandingPrefetches && !decrementOutstandingPrefetches) {
        outstandingPrefetches := outstandingPrefetches + 1
      } elsewhen (!incrementOutstandingPrefetches && decrementOutstandingPrefetches) {
        outstandingPrefetches := outstandingPrefetches - 1
      }

      private def getSkewUsage(set: UInt, skew: UInt): UInt = {
        // Count valid ways in provided skew
        val result = Reg(UInt(log2Up(ways) bits))
        result := 0
        for (i <- 0 until ways) {
          when(cache(set)(skew)(i).valid) {
            result := result + 1
          }
        }

        result
      }

      private def getCacheUsage(): UInt = {
        val max = sets * skews * ways
        val width = log2Up(max + 1)

        val acc = Reg(UInt(width bits)).init(0)

        acc := 0
        for (i <- 0 until sets) {
          for (j <- 0 until skews) {
            for (k <- 0 until ways) {
              when(cache(i)(j)(k).valid) {
                acc := acc + 1
              }
            }
          }
        }

        acc
      }

      private def getSkew(set: UInt): UInt = {
        assert(skews >= 2) // This function should only be called when multiple skews are used

        if (skewApproach == SkewApproach.RS) {
          // Random Selection
          val (rngValid, rngValue) = rng.get()
          assert(rngValid, "Invalid rng value generated")

          (rngValue % skews).resize(log2Up(skews) bits)
        } else {
          // Load Aware
          // Calculate usage of skews
          val usage = (0 until skews).map { i =>
            val s = new SkewUsage()
            s.skew := U(i, log2Up(skews) bits)
            s.usage := getSkewUsage(set, U(i, log2Up(skews) bits))
            s
          }

          // Find skew with lowest usage
          val best = usage.reduceBalancedTree((a, b) => Mux(a.usage < b.usage, a, b))

          best.skew
        }
      }

      private def oldestWay(set: UInt): WayResult = {
        val result = Reg(WayResult())
        result.set := set

        for (i <- 0 until skews) {
          for (j <- 0 until ways) {
            when(cache(set)(i)(j).age === ways - 1 || !cache(set)(i)(j).valid) {
              result.skew := i
              result.way := j
            }
          }
        }

        result
      }

      private def increaseAgesUpTo(set: UInt, oldest: UInt): Unit = {
        for (j <- 0 until skews) {
          for (i <- 0 until ways) {
            when(cache(set)(j)(i).age < oldest) {
              cache(set)(j)(i).age := cache(set)(j)(i).age + 1
            }
          }
        }
      }

      private def decreaseAgesUntil(set: UInt, youngest: UInt): Unit = {
        for (j <- 0 until skews) {
          for (i <- 0 until ways) {
            when(cache(set)(j)(i).age > youngest) {
              cache(set)(j)(i).age := cache(set)(j)(i).age - 1
            }
          }
        }
      }

      private def evictWayGlobal(): WayResult = {
        // Evicts a Way Globally -> Choose Randomly
        val result = WayResult()

        val (rngValid, rngValue) = rng.get()
        assert(rngValid, "Received an invalid rng value") // TODO: Handle invalid rng value

        result.way := rngValue(log2Up(ways) - 1 downto 0).resized
        result.skew := rngValue(log2Up(ways) + log2Up(skews) - 1 downto log2Up(ways)).resized
        result.set := rngValue(
          log2Up(sets) + log2Up(ways) + log2Up(skews) - 1 downto log2Up(ways) + log2Up(skews)
        ).resized

        result
      }

      private def evictWayLocal(setIndex: UInt): WayResult = {
        // Evicts a Way for a Given Set and Skew
        if (replacementPolicy == ReplacementPolicy.RPLRU) {
          // Least Recently Used Approach
          val wayResult = oldestWay(setIndex)
          cache(setIndex)(wayResult.skew)(wayResult.way).valid := False
          increaseAgesUpTo(setIndex, ways * skews - 1)

          return wayResult
        } else {
          // Random Approach
          val (rngValid, rngValue) = rng.get()
          assert(rngValid, "Received an invalid rng value") // TODO: Handle invalid rng value

          val wayResult = WayResult()

          wayResult.set := setIndex
          wayResult.way := rngValue(log2Up(ways) downto 0).resized
          wayResult.skew := rngValue(rngValue.high downto rngValue.high - log2Up(skews)).resized
          cache(setIndex)(wayResult.skew)(wayResult.way).valid := False

          return wayResult
        }
      }

      private val sendingImmediateCmd = Bool()
      private val sendingBufferedCmd = Reg(Bool()).init(False)
      private val cmdBuffer = Reg(MemBusCmd(internal.config))

      // rsp sending buffer
      private val sendingRsp = Bool()
      sendingRsp := False
      private val alreadySendingRsp = Reg(Bool()).init(False)
      private val rspBuffer = Reg(MemBusRsp(internal.config))
      private val returningCache = Reg(Bool()).init(False)

      // Delay cache response with a fixed delay
      // Note that a minimal delay of 1 clock cycle is required to prevent
      // combinatorial loops in case of multiple dbus filters.
      private val internalRspBuffer = Stream(MemBusRsp(internal.config))
      internal.rsp << internalRspBuffer.delay(delay)

      // initial state: not sending or acknowledging anything
      internalRspBuffer.valid := False
      internalRspBuffer.payload.assignDontCare()
      internal.cmd.ready := False
      external.cmd.valid := False
      external.cmd.payload.assignDontCare()
      external.rsp.ready := False

      sendingImmediateCmd := False

      private case class OutstandingTracker() extends Bundle {
        val address: UInt = UInt(config.xlen bits)
        val storeInvalidated: Bool = Bool()
        val pending: Bool = Bool()
        val isPrefetch: Bool = Bool()
        val internalIds: Bits = Bits(1 << internal.config.idWidth bits)
      }

      private val outstandingLoads = Vec.fill(maxId + 1)(RegInit(OutstandingTracker().getZero))

      private def forwardRspToInternal(): Unit = {
        sendingRsp := True
        internalRspBuffer.valid := True

        internalRspBuffer.rdata := external.rsp.rdata
        // the index of 1's in internalIds indicate to which internal ids the response should be forwarded
        val internalId = OHToUInt(OHMasking.first(outstandingLoads(external.rsp.id).internalIds))
        internalRspBuffer.id := internalId
        when(internalRspBuffer.ready) {
          // set the bit to 0 once it has been forwarded
          outstandingLoads(external.rsp.id).internalIds(internalId) := False
        }
      }

      private def insertRspInCache(address: UInt): Unit = {
        val setIndex = getSetIndex(address)
        val tag = getTagBits(address)
        val skew = if (skews >= 2) getSkew(setIndex) else U(0, log2Up(skews) bits)

        outstandingLoads(external.rsp.id).pending := False
        outstandingLoads(external.rsp.id).storeInvalidated := False
        // make sure we don't insert values that have been overwritten with a store
        // either before or in the current cycle
        when(
          !outstandingLoads(external.rsp.id).storeInvalidated &&
            cacheable(address) &&
            !(storeInCycle &&
              getSignificantBits(address) === getSignificantBits(internal.cmd.address))
        ) {
          var stored = False
          var evict = False
          for (i <- 0 until ways) {
            when(cache(setIndex)(skew)(i).valid === False) {
              // Free Entry Found -> Insert Here
              cache(setIndex)(skew)(i).valid := True
              cache(setIndex)(skew)(i).tag := tag
              cache(setIndex)(skew)(i).value := external.rsp.rdata
              cache(setIndex)(skew)(i).age := U(0).resized
              stored = True

              validTags := validTags + 1 // May Trigger an Eviction
              if (replacementPolicy == ReplacementPolicy.RPLRU) {
                increaseAgesUpTo(
                  setIndex,
                  ways * skews - 1
                ) // Increase Ages when PLRU is used
              }
            }
          }

          when(stored === False) {
            // No Free Ways -> Evict Way and use evicted entry
            val wayResult = evictWayLocal(setIndex)
            increaseAgesUpTo(setIndex, cache(setIndex)(wayResult.skew)(wayResult.way).age)

            cache(setIndex)(wayResult.skew)(wayResult.way).valid := True
            cache(setIndex)(wayResult.skew)(wayResult.way).tag := tag
            cache(setIndex)(wayResult.skew)(wayResult.way).value := external.rsp.rdata
            cache(setIndex)(wayResult.skew)(wayResult.way).age := U(0).resized
          }

          if (invalidTags != 0) {
            // Logic only required when invalid tags in use
            when(getCacheUsage() + invalidTags > totalWays) {
              // Valid Tag count has been exceeded
              if (evictionPolicy == EvictionPolicy.LE) {
                // Local Eviction
                evictWayLocal(setIndex)
              } else {
                // Global Eviction
                evictWayGlobal()
              }
            }
          }
        }
        external.rsp.ready := True
      }

      // handling an incoming result from the memory
      when(external.rsp.valid) {
        val address = outstandingLoads(external.rsp.id).address

        when(!alreadySendingRsp) {
          prefetcher foreach { pref =>
            when(outstandingLoads(external.rsp.id).isPrefetch) {
              // inform prefetcher of prefetch response from memory
              pref.notifyPrefetchResponseFromMemory(address, external.rsp.rdata)
              // subscract 1 from outstandingPrefetches
              decrementOutstandingPrefetches := True
            } otherwise {
              // inform prefetcher of load response from memory
              pref.notifyLoadResponseFromMemory(address, external.rsp.rdata)
            }
          }
        }

        when(outstandingLoads(external.rsp.id).internalIds === 0) {
          // store result in cache without forwarding
          insertRspInCache(address)
        } otherwise {
          // forward result and store in cache
          forwardRspToInternal()
          when(
            // when there is only one id left to forward, put result in cache and inform external bus we are done
            internalRspBuffer.ready && CountOne(outstandingLoads(external.rsp.id).internalIds) === 1
          ) {
            insertRspInCache(address)
            alreadySendingRsp := False
          } otherwise {
            alreadySendingRsp := True
          }
        }
      }

      private def returnFromCache(cacheLine: CacheEntry): Unit = {
        // result served from cache
        when(!returningCache) {
          cacheHits := cacheHits + 1
          internal.cmd.ready := True
          rspBuffer.id := internal.cmd.id
          rspBuffer.rdata := cacheLine.value
          when(!sendingRsp) {
            internalRspBuffer.valid := True
            internalRspBuffer.id := internal.cmd.id
            internalRspBuffer.rdata := cacheLine.value
            when(!internalRspBuffer.ready) {
              returningCache := True
            }
          } otherwise {
            returningCache := True
          }
        }
        // if buffer is currently full, we do not ack the cmd, it will stay on the bus for the next cycle
      }

      when(returningCache && !sendingRsp) {
        // when not forwarding rsp but have a stored cache hit, return that
        internalRspBuffer.valid := True
        internalRspBuffer.payload := rspBuffer
        when(internalRspBuffer.ready) {
          returningCache := False
        }
      }

      private def initiateCmdForwarding(): Unit = {
        when(!sendingBufferedCmd) {
          sendingImmediateCmd := True
          internal.cmd.ready := True
          external.cmd.valid := True

          cmdBuffer := external.cmd.payload

          external.cmd.address := internal.cmd.address
          external.cmd.id := externalId

          if (internal.config.readWrite) {
            external.cmd.write := internal.cmd.write
            external.cmd.wdata := internal.cmd.wdata
            external.cmd.wmask := internal.cmd.wmask

            when(!internal.cmd.write) {
              outstandingLoads(externalId).address := internal.cmd.address
              outstandingLoads(externalId).pending := True
              outstandingLoads(externalId).isPrefetch := False
              outstandingLoads(externalId).internalIds := B(0).resized
              outstandingLoads(externalId).internalIds(internal.cmd.id) := True
              externalId := externalId + 1
            }
          } else {
            outstandingLoads(externalId).address := internal.cmd.address
            outstandingLoads(externalId).pending := True
            outstandingLoads(externalId).isPrefetch := False
            outstandingLoads(externalId).internalIds := B(0).resized
            outstandingLoads(externalId).internalIds(internal.cmd.id) := True
            externalId := externalId + 1
          }
          when(!external.cmd.ready) {
            sendingBufferedCmd := True
          }
        }
      }

      when(sendingBufferedCmd) {
        external.cmd.valid := True
        external.cmd.payload := cmdBuffer
        when(external.cmd.ready) {
          sendingBufferedCmd := False
        }
      }

      private def wayForAddress(address: UInt): Flow[WayResult] = {
        val set = cache(getSetIndex(address))
        val tag = getTagBits(address)
        val result = Flow(WayResult())
        result.setIdle()
        for (j <- 0 until skews) {
          for (i <- 0 until ways) {
            when(set(j)(i).valid && set(j)(i).tag === tag) {
              val wayResult = WayResult()
              wayResult.set := getSetIndex(address)
              wayResult.skew := j
              wayResult.way := i
              result.push(wayResult)
            }
          }
        }

        result
      }

      prefetcher foreach { pref =>
        when(
          !sendingBufferedCmd && !sendingImmediateCmd && outstandingPrefetches < maxPrefetches && pref.hasPrefetchTarget
        ) {
          when(!outstandingLoads(externalId).pending) {
            // at this point the cache is ready to send a prefetch command to the memory
            // getNextPrefetchTarget should not be called before the cache is ready to send the command
            // otherwise the prefetch may get lost
            val prefetchAddress = pref.getNextPrefetchTarget

            when(cacheable(prefetchAddress)) {
              val targetWay = wayForAddress(prefetchAddress)
              val setIndex = getSetIndex(prefetchAddress)
              val tagBits = getTagBits(prefetchAddress)

              val alreadyPending = False

              // find out if a load request for the given address is already pending
              for (i <- 0 until outstandingLoads.length) {
                val load = outstandingLoads(i)
                when(
                  getSignificantBits(load.address) === U(
                    tagBits ## setIndex
                  ) && load.pending && !load.storeInvalidated
                ) {
                  alreadyPending := True
                }
              }
              when(!targetWay.valid && !alreadyPending) {
                // add 1 to outstandingPrefetches
                incrementOutstandingPrefetches := True

                externalId := externalId + 1

                external.cmd.valid := True
                external.cmd.address := prefetchAddress
                external.cmd.id := externalId
                cmdBuffer := external.cmd.payload

                outstandingLoads(externalId).address := prefetchAddress
                outstandingLoads(externalId).pending := True
                outstandingLoads(externalId).internalIds := B(0).resized
                outstandingLoads(externalId).isPrefetch := True

                when(!external.cmd.ready) {
                  sendingBufferedCmd := True
                }
              }
            }
          }
        }
      }

      private def getResult(address: UInt): Unit = {
        // inform prefetcher of load request
        prefetcher foreach { pref =>
          pref.notifyLoadRequest(address)
        }

        val targetWay = wayForAddress(address) // Flow[WayResult]
        val setIndex = getSetIndex(address)
        val cacheSet = cache(setIndex)
        val tagBits = getTagBits(address)

        when(targetWay.valid) {
          cacheSet(targetWay.payload.skew)(targetWay.payload.way).age := U(0).resized
          increaseAgesUpTo(
            setIndex,
            cacheSet(targetWay.payload.skew)(targetWay.payload.way).age
          )

          returnFromCache(cacheSet(targetWay.payload.skew)(targetWay.payload.way))
        } otherwise {
          val alreadyPending = False
          for (i <- 0 until outstandingLoads.length) {
            val load = outstandingLoads(i)
            when(
              getSignificantBits(load.address) === U(
                tagBits ## setIndex
              ) && load.pending && !load.storeInvalidated
            ) {
              alreadyPending := True
              // if the load is already pending but result not yet received: mark it to be forwarded + increase cache misses
              when(
                !(external.rsp.valid && getSignificantBits(load.address) === getSignificantBits(
                  outstandingLoads(external.rsp.id).address
                ))
              ) {
                load.internalIds(internal.cmd.id) := True
                cacheMisses := cacheMisses + 1
                forwardedLoads := forwardedLoads + 1
                internal.cmd.ready := True
              }
            }
          }
          // there's no pending load for the same cache line and the bus id is free:
          when(!alreadyPending && !outstandingLoads(externalId).pending) {
            // initiateCmdForwarding() will only go through when !sendingBufferedCmd,
            // also add this check here to only increase cache misses once per request
            when(!sendingBufferedCmd) {
              // increase cache misses
              cacheMisses := cacheMisses + 1
            }

            // forward cmd to external bus
            initiateCmdForwarding()
          }
        }
      }

      // handling a load/write request from the CPU
      when(internal.cmd.valid) {
        val indexBits = getSetIndex(internal.cmd.address)
        val tagBits = getTagBits(internal.cmd.address)

        if (internal.config.readWrite) {
          when(internal.cmd.write) {
            storeInCycle := True
            // write command: invalidates line and forwards to external bus
            for (j <- 0 until skews) {
              for (i <- 0 until ways) {
                when(cache(indexBits)(j)(i).tag === tagBits) {
                  when(cache(indexBits)(j)(i).valid === True) {
                    // Invalidate Line
                    validTags := validTags - 1 // Decrease Valid Tag Counter
                    cache(indexBits)(j)(i).valid := False
                  }

                  // Maximize Age of Line
                  decreaseAgesUntil(indexBits, cache(indexBits)(j)(i).age)
                  cache(indexBits)(j)(i).age := U(ways - 1, log2Up(sets * skews * ways) bits)
                }
              }
            }

            for (i <- 0 until outstandingLoads.length) {
              when(
                getSignificantBits(outstandingLoads(i).address) === getSignificantBits(
                  internal.cmd.address
                ) && outstandingLoads(i).pending
              ) {
                outstandingLoads(i).storeInvalidated := True
              }
            }

            initiateCmdForwarding()
            // if currently forwarding a cmd, we do not ack it, it will stay on the bus for the next cycle
          } otherwise {
            getResult(internal.cmd.address)
          }
        } else {
          getResult(internal.cmd.address)
        }
      }
    }
    cacheArea.setName("cache_" + external.name)
  }

  /** PHASES * */
  override def build(): Unit = {
    busFilter(connect)
  }
}

```


presentation compiler configuration:
Scala version: 2.12.18
Classpath:
<WORKSPACE>/core/.bloop/core/bloop-bsp-clients-classes/classes-Metals-6AJ9Rn88QKuxL7SMty47Ig== [exists ], <HOME>/Library/Caches/bloop/semanticdb/com.sourcegraph.semanticdb-javac.0.11.2/semanticdb-javac-0.11.2.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/scala-lang/scala-library/2.12.18/scala-library-2.12.18.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/spinalhdl/spinalhdl-core_2.12/1.13.0/spinalhdl-core_2.12-1.13.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/spinalhdl/spinalhdl-lib_2.12/1.13.0/spinalhdl-lib_2.12-1.13.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/spinalhdl/spinalhdl-idsl-plugin_2.12/1.13.0/spinalhdl-idsl-plugin_2.12-1.13.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/spinalhdl/spinalhdl-sim_2.12/1.13.0/spinalhdl-sim_2.12-1.13.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/scalactic/scalactic_2.12/3.2.10/scalactic_2.12-3.2.10.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/scala-lang/scala-reflect/2.12.18/scala-reflect-2.12.18.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/scopt/scopt_2.12/4.1.0/scopt_2.12-4.1.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/lihaoyi/sourcecode_2.12/0.3.0/sourcecode_2.12-0.3.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/commons-io/commons-io/2.11.0/commons-io-2.11.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/scala-lang/scala-compiler/2.12.18/scala-compiler-2.12.18.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/spinalhdl/spinalhdl-idsl-payload_2.12/1.13.0/spinalhdl-idsl-payload_2.12-1.13.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/net/openhft/affinity/3.23.2/affinity-3.23.2.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/slf4j/slf4j-simple/2.0.5/slf4j-simple-2.0.5.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/com/github/oshi/oshi-core/6.4.0/oshi-core-6.4.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/scala-lang/modules/scala-xml_2.12/2.1.0/scala-xml_2.12-2.1.0.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/org/slf4j/slf4j-api/2.0.5/slf4j-api-2.0.5.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/net/java/dev/jna/jna/5.12.1/jna-5.12.1.jar [exists ], <HOME>/Library/Caches/Coursier/v1/https/repo1.maven.org/maven2/net/java/dev/jna/jna-platform/5.12.1/jna-platform-5.12.1.jar [exists ]
Options:
-Yrangepos -Xplugin-require:semanticdb




#### Error stacktrace:

```
scala.collection.immutable.Nil$.head(List.scala:469)
	scala.collection.immutable.Nil$.head(List.scala:466)
	scala.reflect.internal.tpe.FindMembers$FindMember.addMemberIfNew(FindMembers.scala:292)
	scala.reflect.internal.tpe.FindMembers$FindMemberBase.walkBaseClasses(FindMembers.scala:124)
	scala.reflect.internal.tpe.FindMembers$FindMemberBase.searchConcreteThenDeferred(FindMembers.scala:77)
	scala.reflect.internal.tpe.FindMembers$FindMemberBase.apply(FindMembers.scala:55)
	scala.reflect.internal.Types$Type.$anonfun$findMember$1(Types.scala:1043)
	scala.reflect.internal.Types$Type.findMemberInternal$1(Types.scala:1041)
	scala.reflect.internal.Types$Type.findMember(Types.scala:1046)
	scala.reflect.internal.Types$Type.memberBasedOnName(Types.scala:672)
	scala.reflect.internal.Types$Type.nonLocalMember(Types.scala:663)
	scala.tools.nsc.typechecker.Typers$Typer.member(Typers.scala:685)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typed1$54(Typers.scala:5098)
	scala.tools.nsc.typechecker.Typers$Typer.typedSelect$1(Typers.scala:5098)
	scala.tools.nsc.typechecker.Typers$Typer.typedSelectOrSuperCall$1(Typers.scala:5226)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5768)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typed1$39(Typers.scala:4918)
	scala.tools.nsc.typechecker.Typers$Typer.silent(Typers.scala:713)
	scala.tools.nsc.typechecker.Typers$Typer.normalTypedApply$1(Typers.scala:4920)
	scala.tools.nsc.typechecker.Typers$Typer.typedApply$1(Typers.scala:4950)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5767)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typedArg$1(Typers.scala:3374)
	scala.tools.nsc.typechecker.Typers$Typer.typedArg(Typers.scala:491)
	scala.tools.nsc.typechecker.Typers$Typer.typedArg0$1(Typers.scala:3512)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$doTypedApply$14(Typers.scala:3531)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$doTypedApply$13(Typers.scala:3502)
	scala.tools.nsc.typechecker.Contexts$Context.$anonfun$savingUndeterminedTypeParams$1(Contexts.scala:360)
	scala.tools.nsc.typechecker.Contexts$Context.savingUndeterminedTypeParams(Contexts.scala:424)
	scala.tools.nsc.typechecker.Typers$Typer.handleOverloaded$1(Typers.scala:3500)
	scala.tools.nsc.typechecker.Typers$Typer.doTypedApply(Typers.scala:3544)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typed1$26(Typers.scala:4853)
	scala.tools.nsc.typechecker.Typers$Typer.silent(Typers.scala:713)
	scala.tools.nsc.typechecker.Typers$Typer.tryTypedApply$1(Typers.scala:4853)
	scala.tools.nsc.typechecker.Typers$Typer.normalTypedApply$1(Typers.scala:4937)
	scala.tools.nsc.typechecker.Typers$Typer.typedApply$1(Typers.scala:4950)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5767)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Typers$Typer.computeType(Typers.scala:5887)
	scala.tools.nsc.typechecker.Namers$Namer.assignTypeToTree(Namers.scala:1114)
	scala.tools.nsc.typechecker.Namers$Namer.valDefSig(Namers.scala:1733)
	scala.tools.nsc.typechecker.Namers$Namer.memberSig(Namers.scala:1919)
	scala.tools.nsc.typechecker.Namers$Namer.typeSig(Namers.scala:1870)
	scala.tools.nsc.typechecker.Namers$Namer$ValTypeCompleter.completeImpl(Namers.scala:945)
	scala.tools.nsc.typechecker.Namers$LockingTypeCompleter.complete(Namers.scala:2082)
	scala.tools.nsc.typechecker.Namers$LockingTypeCompleter.complete$(Namers.scala:2080)
	scala.tools.nsc.typechecker.Namers$TypeCompleterBase.complete(Namers.scala:2075)
	scala.reflect.internal.Symbols$Symbol.completeInfo(Symbols.scala:1542)
	scala.reflect.internal.Symbols$Symbol.info(Symbols.scala:1514)
	scala.reflect.internal.Symbols$Symbol.initialize(Symbols.scala:1698)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5437)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Typers$Typer.typedStat$1(Typers.scala:5876)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typedStats$10(Typers.scala:3356)
	scala.tools.nsc.typechecker.Typers$Typer.typedStats(Typers.scala:3356)
	scala.tools.nsc.typechecker.Typers$Typer.typedTemplate(Typers.scala:2038)
	scala.tools.nsc.typechecker.Typers$Typer.typedClassDef(Typers.scala:1851)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5733)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Typers$Typer.typedStat$1(Typers.scala:5876)
	scala.tools.nsc.typechecker.Typers$Typer.$anonfun$typedStats$10(Typers.scala:3356)
	scala.tools.nsc.typechecker.Typers$Typer.typedStats(Typers.scala:3356)
	scala.tools.nsc.typechecker.Typers$Typer.typedPackageDef$1(Typers.scala:5444)
	scala.tools.nsc.typechecker.Typers$Typer.typed1(Typers.scala:5736)
	scala.tools.nsc.typechecker.Typers$Typer.typed(Typers.scala:5812)
	scala.tools.nsc.typechecker.Analyzer$typerFactory$TyperPhase.apply(Analyzer.scala:114)
	scala.tools.nsc.Global$GlobalPhase.applyPhase(Global.scala:453)
	scala.tools.nsc.interactive.Global$TyperRun.$anonfun$applyPhase$1(Global.scala:1341)
	scala.tools.nsc.interactive.Global$TyperRun.applyPhase(Global.scala:1341)
	scala.tools.nsc.interactive.Global$TyperRun.typeCheck(Global.scala:1334)
	scala.tools.nsc.interactive.Global.typeCheck(Global.scala:666)
	scala.meta.internal.pc.Compat.$anonfun$runOutline$1(Compat.scala:50)
	scala.collection.Iterator.foreach(Iterator.scala:943)
	scala.collection.Iterator.foreach$(Iterator.scala:943)
	scala.collection.AbstractIterator.foreach(Iterator.scala:1431)
	scala.collection.IterableLike.foreach(IterableLike.scala:74)
	scala.collection.IterableLike.foreach$(IterableLike.scala:73)
	scala.collection.AbstractIterable.foreach(Iterable.scala:56)
	scala.meta.internal.pc.Compat.runOutline(Compat.scala:42)
	scala.meta.internal.pc.Compat.runOutline(Compat.scala:28)
	scala.meta.internal.pc.Compat.runOutline$(Compat.scala:26)
	scala.meta.internal.pc.MetalsGlobal.runOutline(MetalsGlobal.scala:39)
	scala.meta.internal.pc.ScalaCompilerWrapper.compiler(ScalaCompilerAccess.scala:18)
	scala.meta.internal.pc.ScalaCompilerWrapper.compiler(ScalaCompilerAccess.scala:13)
	scala.meta.internal.pc.ScalaPresentationCompiler.$anonfun$documentHighlight$1(ScalaPresentationCompiler.scala:527)
	scala.meta.internal.pc.CompilerAccess.withSharedCompiler(CompilerAccess.scala:148)
	scala.meta.internal.pc.CompilerAccess.$anonfun$withInterruptableCompiler$1(CompilerAccess.scala:92)
	scala.meta.internal.pc.CompilerAccess.$anonfun$onCompilerJobQueue$1(CompilerAccess.scala:209)
	scala.meta.internal.pc.CompilerJobQueue$Job.run(CompilerJobQueue.scala:152)
	java.base/java.util.concurrent.ThreadPoolExecutor.runWorker(ThreadPoolExecutor.java:1144)
	java.base/java.util.concurrent.ThreadPoolExecutor$Worker.run(ThreadPoolExecutor.java:642)
	java.base/java.lang.Thread.run(Thread.java:1583)
```
#### Short summary: 

java.util.NoSuchElementException: head of empty list