.. index:: pair: enum; Kit_PlayerState
.. _doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0:
.. _cid-kit_playerstate:

enum Kit_PlayerState
====================



Overview
~~~~~~~~



.. ref-code-block:: cpp
	:class: overview-code-block

	// enum values

	:ref:`KIT_STOPPED<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0a37c33efe5c4ab0e31f08d2896a4c1c97>` = 0
	:ref:`KIT_PLAYING<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0aaff3e9ef5d56a43a94e9cd7bdcea7fca>` 
	:ref:`KIT_PAUSED<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0a1018d4528f321840ea8e3aa24d63a8b1>` 
	:ref:`KIT_CLOSED<doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0af61b5f082f5efefa088235bf838206bd>` 

.. _details-doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0:

Detailed Documentation
~~~~~~~~~~~~~~~~~~~~~~



Enum Values
-----------

.. _doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0a37c33efe5c4ab0e31f08d2896a4c1c97:
.. _cid-kit_playerstate::kit_stopped:
.. ref-code-block:: cpp
	:class: title-code-block

	KIT_STOPPED

Playback stopped or has not started yet.

.. _doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0aaff3e9ef5d56a43a94e9cd7bdcea7fca:
.. _cid-kit_playerstate::kit_playing:
.. ref-code-block:: cpp
	:class: title-code-block

	KIT_PLAYING

Playback started & player is actively decoding.

.. _doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0a1018d4528f321840ea8e3aa24d63a8b1:
.. _cid-kit_playerstate::kit_paused:
.. ref-code-block:: cpp
	:class: title-code-block

	KIT_PAUSED

Playback paused; player is actively decoding but no new data is given out.

.. _doxid-kitplayer_8h_1a02988a97b08c9a74fed9009f64e6a1b0af61b5f082f5efefa088235bf838206bd:
.. _cid-kit_playerstate::kit_closed:
.. ref-code-block:: cpp
	:class: title-code-block

	KIT_CLOSED

Playback is stopped and player is closing.

