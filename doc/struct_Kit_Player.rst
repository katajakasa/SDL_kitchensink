.. index:: pair: struct; Kit_Player
.. _doxid-struct_kit___player:
.. _cid-kit_player:

struct Kit_Player
=================

.. toctree::
	:hidden:

.. code-block:: cpp
	:class: overview-code-block

	#include <kitplayer.h>


Overview
~~~~~~~~



.. ref-code-block:: cpp
	:class: overview-code-block

	// fields

	:ref:`Kit_PlayerState<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0>` :ref:`state<doxid-struct_kit___player_1aeaf3509c9fa8e94895a51617a26e547e>`
	void* :ref:`decoders<doxid-struct_kit___player_1a488627172ff25490c078af37e5990bad>`[3]
	SDL_Thread* :ref:`dec_thread<doxid-struct_kit___player_1ada80740b051471386256483a559f01b5>`
	SDL_mutex* :ref:`dec_lock<doxid-struct_kit___player_1aea2d87aa236c94fee5fa4d552fe877e6>`
	const :ref:`Kit_Source<doxid-struct_kit___source>`* :ref:`src<doxid-struct_kit___player_1a9fac0eb022fd8fec32a3e976140413f3>`
	double :ref:`pause_started<doxid-struct_kit___player_1a3a064f029f174ff2d6df292565f81856>`

.. _details-doxid-struct_kit___player:

Detailed Documentation
~~~~~~~~~~~~~~~~~~~~~~



Fields
------

.. _doxid-struct_kit___player_1aeaf3509c9fa8e94895a51617a26e547e:
.. _cid-kit_player::state:
.. ref-code-block:: cpp
	:class: title-code-block

	:ref:`Kit_PlayerState<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0>` state

Playback state.

.. _doxid-struct_kit___player_1a488627172ff25490c078af37e5990bad:
.. _cid-kit_player::decoders:
.. ref-code-block:: cpp
	:class: title-code-block

	void* decoders [3]

Decoder contexts.

.. _doxid-struct_kit___player_1ada80740b051471386256483a559f01b5:
.. _cid-kit_player::dec_thread:
.. ref-code-block:: cpp
	:class: title-code-block

	SDL_Thread* dec_thread

Decoder thread.

.. _doxid-struct_kit___player_1aea2d87aa236c94fee5fa4d552fe877e6:
.. _cid-kit_player::dec_lock:
.. ref-code-block:: cpp
	:class: title-code-block

	SDL_mutex* dec_lock

Decoder lock.

.. _doxid-struct_kit___player_1a9fac0eb022fd8fec32a3e976140413f3:
.. _cid-kit_player::src:
.. ref-code-block:: cpp
	:class: title-code-block

	const :ref:`Kit_Source<doxid-struct_kit___source>`* src

Reference to Audio/Video source.

.. _doxid-struct_kit___player_1a3a064f029f174ff2d6df292565f81856:
.. _cid-kit_player::pause_started:
.. ref-code-block:: cpp
	:class: title-code-block

	double pause_started

Temporary flag for handling pauses.

